# Userspace and System Calls

Phase 4 brings ring 3. This document covers user address spaces, the
SYSCALL/SYSRET path, the ELF64 loader, and the process model: fork, execve,
wait4, exit over a host-tested process table.

## System call ABI

The native ABI is **identical to the Linux x86_64 syscall convention**, on
purpose: the Phase 7 Linux personality layer then shares one numbering, one
register convention, and one error vocabulary with native code.

| aspect | contract |
|---|---|
| entry | `syscall` instruction |
| number | `rax` |
| arguments | `rdi`, `rsi`, `rdx`, `r10`, `r8`, `r9` |
| return | `rax`; errors are `-errno` (see `kernel/include/errno.h`) |
| clobbered | `rcx` (return RIP), `r11` (RFLAGS) — hardware behavior |
| preserved | every other register — the entry stub saves and restores the full caller-saved set; callee-saved registers survive through the C ABI |
| unimplemented | `-ENOSYS`, always |

Implemented today (numbers from the Linux x86_64 table):

| # | syscall | scope |
|---|---|---|
| 1 | `write(fd, buf, count)` | fd 1/2 → kernel console; full validation, returns count |
| 39 | `getpid()` | the calling process's pid |
| 57 | `fork()` | full process clone (eager copy; COW later); child gets rax = 0 |
| 59 | `execve(path, argv, envp)` | replace the image from the built-in program table; SysV argv/envp stack |
| 60 | `exit(status)` | zombie in the table, parent woken |
| 61 | `wait4(pid, wstatus, 0, NULL)` | blocking reap of one child (pid > 0 exact, -1 any); Linux `WIFEXITED` status encoding |

Every user pointer is validated before use: the range must lie below
`USER_VA_LIMIT` (`0x0000800000000000`, the canonical lower half) and every
page it touches must be present **and** user-accessible (`PTE_US`) in the
caller's address space. Anything else returns `-EFAULT` without being touched.

## The entry path

`syscall` does not switch stacks, so the entry stub
(`kernel/arch/x86_64/syscall_entry.asm`) runs its first instructions on the
*user* stack pointer with interrupts masked by SFMASK (`IF|TF|DF|AC`). It
parks the user RSP, adopts the current thread's kernel stack from a global the
scheduler maintains on every context switch (single CPU; `swapgs` + per-CPU
state arrive with SMP), saves every caller-saved user register (the ABI
promises they survive; the C dispatch would trash them), re-enables
interrupts — syscalls may block or be preempted — and calls
`syscall_dispatch()`. The return path disables interrupts, restores the
saved registers and the user RSP/RIP/RFLAGS, and `sysret`s. Init asserts
this contract at boot: it issues a syscall with sentinel values in all six
argument registers and verifies them afterwards.

MSR setup (`usermode.c`): `EFER.SCE`, `STAR` (selector layout in `gdt.h` was
designed for SYSRET's `+8/+16` selector math back in Phase 2), `LSTAR` →
entry stub, `SFMASK`.

The entry stub's register save area *is* `struct syscall_frame` (layout
asserted with `offsetof`): the complete user context, which is what makes
fork a struct copy. All kernel→ring 3 entries go through
`user_frame_enter(frame)` — an `iretq` with the frame's full register state.
A process's first entry is simply a zeroed frame with `rip`/`rsp`/`rflags`
set, so nothing of the kernel leaks into ring 3.

## Address spaces

`vmm_addrspace_create_user()` builds a PML4 whose upper half aliases the
kernel PML4's entries 256–511: the kernel is mapped (so interrupts, syscalls,
and the scheduler need no CR3 gymnastics) but inaccessible from ring 3 (no
kernel mapping carries `PTE_US`). Consequence, enforced by convention: the
kernel never adds *new top-level* PML4 entries after `vmm_init()` — all kernel
mappings live under the HHDM and kernel-image slots populated there.

The scheduler tracks an address space per thread (`NULL` = kernel). On a
context switch it reloads CR3 only when the target differs from what is
active, so pure kernel threads never pay for a TLB flush, and it always points
`TSS.rsp0` and the syscall stack global at the incoming thread's kernel stack.

User page permissions are real: code pages RX (no `PTE_W`), stack/data pages
RW + `PTE_NX`, and the page-table walk propagates `PTE_US` through the
intermediate levels while leaf entries decide the effective permission.

## Program loading: ELF64

Programs are statically linked ELF64 executables (`ET_EXEC`, `EM_X86_64`).
Loading is split the same way as every other core/kernel pair in the tree:

- **`elf64_validate()`** (`kernel/lib/elf64.c`) is a pure function over the
  image bytes, compiled for the host and tested under ASan/UBSan with a
  crafted well-formed executable plus one targeted mutation per rejection
  path. It checks the identity fields (magic, class, endianness, version,
  type, machine), the program header table bounds, and every `PT_LOAD`
  segment: file ranges in bounds (overflow-safely), `filesz ≤ memsz`, the
  vaddr range inside `[PAGE_SIZE, USER_VA_LIMIT)` (the null page is never
  mappable), vaddr/offset page congruence, no two segments sharing a page,
  no writable+executable segment, and an entry point inside an executable
  segment's file-backed bytes. The contract: after `ELF64_OK`, every
  arithmetic step the loader performs is overflow-free and in-bounds.
- **`elf64_load()`** (`kernel/proc/elf_load.c`) materializes a validated
  image: fresh zeroed frames per page (so `memsz > filesz` bss tails and
  segment padding arrive zeroed), file bytes copied in, and leaf permissions
  derived per segment — `PF_X` clears NX, `PF_W` sets writable, everything
  else is read-only NX. Rejection reasons surface as `elf64_strerror()` text.

`user/user.ld` links user programs at `0x400000` with three page-aligned
`PT_LOAD` segments (text R+X, rodata R, data+bss RW) so the per-segment W^X
policy is exercised by the very first binary. The ELF headers themselves are
not mapped into user memory; the kernel reads them from the embedded image.

## The process model

The process table itself is a pure state machine (`kernel/proc/proc_core.c`,
host-tested): up to 64 processes, monotonically increasing pids starting at
1 (init), parent links, `LIVE → ZOMBIE → free` lifecycle, and orphan
reparenting to init on parent exit. The kernel wraps it
(`kernel/proc/process.c`) with what a pure table cannot hold: the address
space, the hosting kernel thread, the blocked waiter, and the process name.

Every process is **hosted by a kernel thread**. The thread builds nothing
itself — it receives a completed image (address space + start frame),
binds to the address space (`sched_set_addrspace`), and enters ring 3 via
`user_frame_enter`, never to return except through syscalls. This gives
processes preemption, sleep/wake, and join for free from the Phase 3
scheduler; the thread carries its process's pid.

The lifecycle syscalls:

- **`fork`** allocates a child pid, clones the address space eagerly
  (`paging_user_clone`: every lower-half 4 KiB mapping copied to a fresh
  frame, W/US/NX permissions preserved — COW comes later), copies the
  parent's saved syscall frame with `rax = 0`, and launches a hosting
  thread. The full-trapframe entry path is what makes this a struct copy.
- **`execve`** copies the path and both string vectors out of user memory
  first (bounded: 16 args, 128 bytes of strings, path ≤ 63), then builds the
  **complete new image before touching the old one** — address space, ELF
  segments, stack, argv/envp — so any failure (`-ENOENT`, `-ENOMEM`,
  `-E2BIG`) returns with the caller unharmed. On success it swaps the
  address space, reloads CR3, destroys the old space, and rewrites the saved
  syscall frame so `sysret` lands at the new entry point. Programs come from
  a built-in table (`/bin/init`, `/bin/hello`) until the VFS exists.
- **`exit`** marks the process a zombie holding its status, reparents its
  children to init, wakes the parent if it is blocked in `wait4`, and ends
  the hosting thread.
- **`wait4`** finds a matching child (exact pid or `-1` for any): a zombie
  is reaped — join the hosting thread, destroy the address space, free the
  table slot, deliver the Linux wait status (`(code & 0xff) << 8` for a
  normal exit, the signal number for a fault kill — `WIFEXITED` /
  `WIFSIGNALED` semantics); otherwise the parent publishes itself as the
  waiter and blocks. The check and the block happen in one interrupts-off
  section, so a child exiting concurrently cannot slip through unnoticed.

A hardware exception raised in ring 3 is never the kernel's problem: the
trap dispatcher logs one diagnostic line (exception, `rip`, error code,
`cr2` for page faults) and **kills the offending process** with the Linux
signal for that exception (`#PF`/`#GP` → `SIGSEGV`, `#UD` → `SIGILL`,
`#DE`/`#MF`/`#XM` → `SIGFPE`, ...), delivered to the parent through
`wait4`. The kernel and every other process keep running. Only
machine-level events (NMI, double fault, machine check) still panic, as
does any fault taken in kernel mode — that is a kernel bug by definition.
If init itself dies by signal, the kernel panics, Unix style.

New images start with the System V ABI stack contract: `[argc, argv...,
NULL, envp..., NULL, AT_NULL]` at a 16-byte-aligned `rsp`, string bytes
packed in the top stack page. `user/crt0.asm` picks `argc`/`argv` from it
and calls `main(argc, argv)`.

```
0x0000000000400000   .text    R+X
0x0000000000401000   .rodata  R
0x0000000000402000   .data + .bss  RW + NX
0x00007FFFFFFFB000   stack, 4 pages RW + NX
0x00007FFFFFFFF000   stack top; initial RSP just below, after argv/envp
```

## Init

Init is a freestanding C program: `user/crt0.asm` plus `user/init.c`, with
syscall wrappers in `user/syscall.h` and a tiny `user/ulib.h`. User code is
compiled without SSE and without the red zone — the kernel does not save
FPU/SSE state across context switches, and interrupts run on the current
stack. The linked ELFs (`init.elf`, `hello.elf`) are embedded in kernel
`.rodata` (`kernel/user_blob.asm`); boot loads init and joins it:

```
user: launching init (embedded ELF, 13344 bytes)
hello from ring 3
hello from execve
user: C init: .data .bss .rodata ok
user: init exited (status 0)
```

Init doubles as the acceptance test for the loader, the ABI, and the
process model. Its exit status names the first failed check; 0 means all
twenty passed: `write` returns the full length, `.bss` zero-filled, `.data`
initialized and writable, `getpid`, `-ENOSYS`/`-EBADF`/`-EFAULT` error
paths, all six argument registers surviving a syscall, the full process
round trip — `fork` returns a fresh pid, the child `execve`s `/bin/hello`
(which verifies its own argv arrived intact and exits 42), `wait4` returns
that pid with status `42 << 8`, a second `wait4` returns `-ECHILD`,
`execve` of an unknown path returns `-ENOENT` — and fault isolation: a
forked child that writes to a kernel address is killed with `SIGSEGV`, one
that executes an illegal instruction with `SIGILL`, both observed through
`wait4` while everything else keeps running. After init is reaped, the
kernel asserts the process table is empty and that the physical frame count
matches the pre-launch value: the whole fork/exec/wait cycle leaks nothing.

## Known limits of this slice (by design, lifted in later slices)

- Static `ET_EXEC` only; `ET_DYN`/interpreters are Phase 7 territory.
- `execve` loads from a built-in program table; real paths arrive with the
  VFS (Phase 5).
- `fork` copies eagerly; no copy-on-write yet.
- `wait4` supports options 0 and a NULL rusage only; no `WNOHANG`, no
  process groups. No signal *delivery* exists yet — signal numbers appear
  only as fault-kill wait statuses.
- No SMEP/SMAP yet; the kernel relies on paging permissions plus pointer
  validation.
- The kernel does not save FPU/SSE state, so user code is built `-mno-sse`
  (enforced by `USER_CFLAGS`); lazy FPU switching comes later.
