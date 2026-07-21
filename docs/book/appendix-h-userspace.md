# Userspace and System Calls

Phase 4 brings ring 3; Phase 5 gives it files. This document covers user
address spaces, the SYSCALL/SYSRET path, the ELF64 loader, the process model —
fork, execve, wait4, exit over a host-tested process table — and the file
descriptor layer over the VFS (Appendix J).

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
| 0 | `read(fd, buf, count)` | any readable fd; console reads block until input, then short-read |
| 1 | `write(fd, buf, count)` | any writable fd; disk writes are durable before this returns |
| 2 | `open(path, flags, mode)` | access mode + `O_CREAT`/`O_EXCL`/`O_TRUNC`/`O_APPEND`/`O_DIRECTORY`; other flags refused `-EINVAL` |
| 3 | `close(fd)` | drops the fd table's reference; last reference frees the description |
| 5 | `fstat(fd, statbuf)` | the Linux x86_64 144-byte `struct stat` (`_Static_assert`ed) |
| 8 | `lseek(fd, off, whence)` | SET/CUR/END on files and directories; `-ESPIPE` on the console |
| 22 | `pipe(fds[2])` | anonymous pipe: `fds[0]` read end, `fds[1]` write end (Appendix J) |
| 39 | `getpid()` | the calling process's pid |
| 57 | `fork()` | full process clone (eager copy; COW later); child gets rax = 0 |
| 59 | `execve(path, argv, envp)` | replace the image with the ELF at `path` on the filesystem; SysV argv/envp stack; fds survive |
| 60 | `exit(status)` | zombie in the table, parent woken, every fd closed |
| 61 | `wait4(pid, wstatus, 0, NULL)` | blocking reap of one child (pid > 0 exact, -1 any); Linux `WIFEXITED` status encoding |
| 74 | `fsync(fd)` | 0 on graphfs objects (already durable); `-EINVAL` on the console |
| 82 | `rename(old, new)` | atomic move/replace with POSIX semantics; `-EXDEV` across mounts |
| 83 | `mkdir(path, mode)` | one atomic create+link transaction |
| 84 | `rmdir(path)` | empty directories only; mount points are `-EBUSY` |
| 86 | `link(old, new)` | hard links to regular files; directories are `-EPERM` |
| 87 | `unlink(path)` | removes one name; last name of an *open* file is `-EBUSY` (v1 policy, Appendix J) |
| 217 | `getdents64(fd, buf, count)` | real `linux_dirent64` records; directories synthesize `.` and `..` |

The fd-backed syscalls dispatch through the VFS `struct file_ops` vtable; a
`NULL` slot maps to the conventional errno (`write` on a directory `-EBADF`,
`lseek` on the console `-ESPIPE`, `getdents64` on a regular file `-ENOTDIR`,
`fsync` on the console `-EINVAL`), and the ops themselves enforce the
description's access mode with `-EBADF`. The full open-file semantics live
in Appendix J.

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
not mapped into user memory; the kernel parses them from the kernel-side
buffer `vfs_read_file()` filled from disk.

## The process model

The process table itself is a pure state machine (`kernel/proc/proc_core.c`,
host-tested): up to 64 processes, monotonically increasing pids starting at
1 (init), parent links, `LIVE → ZOMBIE → free` lifecycle, and orphan
reparenting to init on parent exit. The kernel wraps it
(`kernel/proc/process.c`) with what a pure table cannot hold: the address
space, the hosting kernel thread, the blocked waiter, and the process name.

Each process also owns a **file descriptor table** (`FD_MAX` = 16 slots of
`struct file *`). An fd is an index into it; the object it names is a VFS open
file description, refcounted so `fork` can duplicate the table by pointer —
parent and child share each description, offset included, per POSIX. `execve`
leaves the table untouched (no close-on-exec flags yet — documented), and exit
closes everything. Init starts with fds 0/1/2 all referencing one open of
`/dev/console`.

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
  syscall frame so `sysret` lands at the new entry point. The image comes off
  the filesystem: `vfs_read_file()` reads the whole ELF into a kernel buffer,
  the loader materializes it, the buffer is freed.
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
stack. The linked ELFs are installed at `/bin` on the graphfs image
(`build/fs.img`, built by `tools/graphfs_mkfs.c`); boot reads `/bin/init`
through the VFS and joins it:

```
user: launching init (/bin/init from disk, 17688 bytes)
hello from ring 3
hello from execve
user: console open via /dev/console ok
user: C init: .data .bss .rodata ok
user: init exited (status 0)
```

If `vfs_init()` found no disk (Appendix J), there is no `/bin/init` to read:
`process_run_init()` checks `vfs_has_root()` first, logs `process: no root
filesystem, skipping init`, and returns without touching the process table
instead of panicking on the read failure. This is the expected path on real
hardware before an AHCI/NVMe driver exists (Appendix M).

Init doubles as the acceptance test for the loader, the ABI, the process
model, and the whole VFS. Its exit status names the first failed check; 0
means all one hundred four passed: `write` returns the full length, `.bss`
zero-filled, `.data` initialized and writable, `getpid`,
`-ENOSYS`/`-EBADF`/`-EFAULT` error paths, all six argument registers
surviving a syscall, the full process round trip — `fork` returns a fresh
pid, the child `execve`s `/bin/hello` (which verifies its own argv arrived
intact and exits 42), `wait4` returns that pid with status `42 << 8`, a
second `wait4` returns `-ECHILD`, `execve` of an unknown path returns
`-ENOENT` — fault isolation: a forked child that writes to a kernel address
is killed with `SIGSEGV`, one that executes an illegal instruction with
`SIGILL`, both observed through `wait4` while everything else keeps running —
the read surface: a known ELF opened and its magic read, `lseek` END/SET
proven against `fstat`'s size, `/bin` walked with `getdents64` and required
to hold exactly `.`, `..`, `init`, `hello`, `/dev/../bin/./hello` resolving
through the normalizer, and every error contract probed (`-ENOENT`,
`-ENOTDIR`, `-EISDIR`, `-EBADF` after close, `-ESPIPE` on the console) —
and the write path, in a scratch tree init removes without a trace: a file
created with `O_CREAT|O_EXCL`, written, reread byte-for-byte, `fstat`ed and
`fsync`ed; `O_APPEND` landing at EOF; `O_TRUNC` emptying; a hard link
sharing bytes and nlink; rename moving and the moved-into-own-subtree case
refused `-EINVAL`; `mkdir`/`rmdir`/`unlink` with their whole errno
vocabulary (`-EEXIST`, `-ENOTEMPTY`, `-EISDIR`, `-ENOTDIR`, the open-file
`-EBUSY` policy, devfs `-EPERM`, mount-point `-EBUSY`, cross-mount
`-EXDEV`) — and pipes (Appendix J): each end refuses the other's direction
with the right errno, a small write/read round-trips byte-for-byte, a piped
message survives a real `fork` (the child writes and closes both ends, the
parent reads to `EOF` and reaps it through `wait4`), and a write with no
readers left returns `-EPIPE` instead of hanging. After init is reaped, the
kernel asserts the process table is empty and that the physical frame count
matches the pre-launch value: the whole fork/exec/wait cycle leaks nothing.

## Known limits of this slice (by design, lifted in later slices)

- Static `ET_EXEC` only; `ET_DYN`/interpreters are Phase 7 territory.
- `FD_MAX` is 16, there is no `dup`/`dup2`, and no close-on-exec flags.
- No `chdir`: every process's working directory is `/`, and relative paths
  resolve from there (defined, not undefined).
- `fork` copies eagerly; no copy-on-write yet.
- `wait4` supports options 0 and a NULL rusage only; no `WNOHANG`, no
  process groups. No signal *delivery* exists yet — signal numbers appear
  only as fault-kill wait statuses.
- No SMEP/SMAP yet; the kernel relies on paging permissions plus pointer
  validation.
- The kernel does not save FPU/SSE state, so user code is built `-mno-sse`
  (enforced by `USER_CFLAGS`); lazy FPU switching comes later.
- Pipes have no `O_NONBLOCK` (every read/write blocks as needed) and, since
  `dup`/`dup2` don't exist yet, no way to remap a pipe end onto fds 0/1/2
  before an `execve` — the classic shell-pipeline construction needs both.
