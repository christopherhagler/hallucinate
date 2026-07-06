# Userspace and System Calls

Phase 4 brings ring 3. This document covers what is implemented so far — the
first slice: user address spaces, the SYSCALL/SYSRET path, and an embedded
init program — and records the ABI contract the rest of the phase (ELF loader,
process table, fork/exec/wait) builds on.

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
state arrive with SMP), re-enables interrupts — syscalls may block or be
preempted — and calls `syscall_dispatch()`. The return path disables
interrupts, restores the user RSP/RIP/RFLAGS, and `sysret`s.

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

## Init (current slice)

Until the ELF loader lands, init is a flat binary (`user/init.asm`, 92
bytes) assembled at build time, embedded in kernel `.rodata`
(`kernel/user_blob.asm`), and mapped at `0x400000` with a stack page just
below the canonical boundary:

```
0x0000000000400000   code, RX          (one page, blob copied in)
0x00007FFFFFFFE000   stack, RW + NX    (one page)
0x00007FFFFFFFF000   initial RSP
```

A kernel thread hosts the process: it builds the address space, binds to it
(`sched_set_addrspace`), and drops to ring 3, never to return except through
syscalls. `SYS_exit` records the status and ends the hosting thread; the boot
thread joins it, tears the address space down with `paging_user_destroy()`
(every user frame and page table freed — the launch path panics if a single
frame leaks), and logs:

```
user: launching init (embedded, 92 bytes)
hello from ring 3
user: init exited (status 0)
```

Init's exit status is itself a test: it exits 0 only if `write` returned the
full length, `getpid` returned 1, and syscall 999 returned `-ENOSYS`. The QEMU
integration test asserts both lines.

## Known limits of this slice (by design, lifted in later slices)

- One process; PIDs are fixed; no fork/exec/wait yet.
- A user-mode fault (e.g. touching a kernel address) currently panics the
  kernel via the trap path instead of killing the process.
- No SMEP/SMAP yet; the kernel relies on paging permissions plus pointer
  validation.
- Program loading is the embedded blob; the ELF64 loader is next.
