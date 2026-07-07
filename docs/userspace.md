# Userspace and System Calls

Phase 4 brings ring 3. This document covers what is implemented so far — user
address spaces, the SYSCALL/SYSRET path, the ELF64 loader, and a C init
program — and records the ABI contract the rest of the phase (process table,
fork/exec/wait) builds on.

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
| 39 | `getpid()` | init's PID (1) |
| 60 | `exit(status)` | terminates the process; kernel logs the status |

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

The *first* entry to ring 3 goes through `user_enter()` — an `iretq` with a
hand-built frame (user CS/SS at RPL 3, `IF` set) and every register cleared so
nothing of the kernel leaks into ring 3.

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

## Init

Init is a freestanding C program: `user/crt0.asm` (zero the frame pointer,
align the stack, `call main`, `exit(main())`) plus `user/init.c`, with
syscall wrappers in `user/syscall.h`. It is compiled without SSE and without
the red zone — the kernel does not yet save FPU/SSE state across context
switches, and interrupts run on the current stack. The linked `init.elf` is
embedded in kernel `.rodata` (`kernel/user_blob.asm`) and loaded at boot:

```
0x0000000000400000   .text    R+X
0x0000000000401000   .rodata  R
0x0000000000402000   .data + .bss  RW + NX
0x00007FFFFFFFB000   stack, 4 pages RW + NX
0x00007FFFFFFFF000   initial RSP
```

A kernel thread hosts the process: it builds the address space, loads the
ELF, binds to it (`sched_set_addrspace`), and drops to ring 3, never to
return except through syscalls. `SYS_exit` records the status and ends the
hosting thread; the boot thread joins it, tears the address space down with
`paging_user_destroy()` (every user frame and page table freed — the launch
path panics if a single frame leaks), and logs:

```
user: launching init (embedded ELF, 13232 bytes)
hello from ring 3
user: C init: .data .bss .rodata ok
user: init exited (status 0)
```

Init doubles as the acceptance test for the loader and the ABI. Its exit
status names the first failed check; 0 means all of: `write` returned the
full length, `.bss` arrived zero-filled, `.data` arrived initialized and
writable (the second output line is patched in `.data` before printing),
`getpid` returned 1, syscall 999 returned `-ENOSYS`, fd 7 returned `-EBADF`,
an unmapped pointer returned `-EFAULT`, and all six argument registers
survived a syscall unchanged. The QEMU integration test asserts all three
lines.

## Known limits of this slice (by design, lifted in later slices)

- One process; PIDs are fixed; no fork/exec/wait yet.
- Static `ET_EXEC` only; `ET_DYN`/interpreters are Phase 7 territory.
- A user-mode fault (e.g. touching a kernel address) currently panics the
  kernel via the trap path instead of killing the process.
- No SMEP/SMAP yet; the kernel relies on paging permissions plus pointer
  validation.
- The kernel does not save FPU/SSE state, so user code is built `-mno-sse`
  (enforced by `USER_CFLAGS`); lazy FPU switching comes with the process
  table work.
