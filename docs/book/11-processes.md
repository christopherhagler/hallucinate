# Chapter 11 — ELF Loading and Processes

Everything from the last chapter was mechanism: the ability to drop into ring 3
and come back. This chapter builds the *model* on top of it — the Unix process
lifecycle of fork, execve, wait4, and exit — and, just as importantly, makes a
crashing process die alone without taking the kernel with it. This is where the
system finally looks like an operating system from the outside: programs that
start other programs, run in isolation, and are cleaned up when they end.

## 11.1 Loading a program: validate totally, then load trustingly

A program is a statically linked ELF64 executable (`ET_EXEC`, `EM_X86_64`).
Loading it splits into two functions with a deliberate, sharp division of
responsibility — and this split is a masterclass in how to handle untrusted
structured input.

**`elf64_validate()`** (`kernel/lib/elf64.c`) is a *pure* function over the image
bytes — host-tested under ASan/UBSan with a well-formed executable plus one
targeted mutation per rejection path. Its contract is strong and worth quoting in
spirit: after it returns OK, **every subsequent arithmetic step the loader
performs is provably in-bounds and overflow-free.** It checks the identity fields
(magic, class, endianness, version, type, machine), the program-header table
bounds, and for every `PT_LOAD` segment: file ranges in bounds *computed
overflow-safely*, `filesz ≤ memsz`, the virtual range inside
`[PAGE_SIZE, USER_VA_LIMIT)` (so the null page is never mappable and nothing
lands in the kernel half), vaddr/offset page congruence, no two segments sharing
a page, **no writable-and-executable segment** (W^X enforced at load, not just
runtime), and an entry point that lands inside an executable segment's
file-backed bytes.

**`elf64_load()`** (`kernel/proc/elf_load.c`) then materializes a *validated*
image without re-checking anything, because validation already proved it safe:
fresh zeroed frames per page (so bss tails and padding are zero by construction),
file bytes copied in, and leaf page permissions derived per segment — `PF_X`
clears NX, `PF_W` sets writable, everything else read-only-NX.

The pattern here is the one to internalize for *any* parser of hostile input:
**separate total validation from action, and make the validation a pure function
you can exhaustively test.** The validator's job is to establish an invariant
("this structure is well-formed and all these arithmetic relationships hold"); the
loader's job is to act under that invariant without defensive re-checking cluttering
every line. The mutation tests — take a valid image, corrupt exactly one field,
assert the validator rejects it — are how you gain confidence the validator is
*total*, i.e. rejects everything it should. A validator that is merely "usually
right" is worse than none, because it lulls the code downstream into trusting it.

`user/user.ld` links user programs at `0x400000` with three page-aligned
`PT_LOAD` segments — text R+X, rodata R, data+bss RW — precisely so the very
first binary exercises the per-segment W^X policy end to end.

## 11.2 The process table is a pure state machine

The process table itself (`kernel/proc/proc_core.c`, host-tested) is — you can
predict it now — a *pure* state machine: up to 64 processes, monotonically
increasing pids from 1 (init), parent links, a `LIVE → ZOMBIE → free` lifecycle,
and orphan reparenting to init when a parent exits. It touches no hardware, no
address spaces, no threads. That is what lets it be tested exhaustively on the
host, including the fiddly cases: reaping in various orders, reparenting a whole
subtree, pid wraparound behavior.

The kernel wrapper (`kernel/proc/process.c`) adds what a pure table cannot hold:
the address space, the hosting kernel thread, the blocked waiter, the process
name. This is the core/driver split applied to the process abstraction — the
lifecycle *logic* is pure and proven; the *bindings* to real kernel resources
are the thin trusted layer.

**Every process is hosted by a kernel thread.** The thread builds nothing itself:
it receives a completed image (address space plus start frame), binds to the
address space via `sched_set_addrspace`, and enters ring 3 through
`user_frame_enter` (Chapter 10), never to return except through syscalls. This is
the reuse dividend from Chapter 9 — because a process's kernel side *is* just a
thread, processes get preemption, sleep/wake, and join for free from the
scheduler, and the hosting thread simply carries its process's pid. Building the
process model on the thread abstraction instead of parallel to it is why this
phase is small.

## 11.3 The four syscalls, and the discipline in each

**`fork`** (syscall 57) allocates a child pid, clones the parent's address space
eagerly (`paging_user_clone`: every lower-half 4 KiB mapping copied to a fresh
frame with W/US/NX preserved — copy-on-write is a documented later optimization),
copies the parent's saved syscall frame with `rax = 0`, and launches a hosting
thread that resumes the child at the copied frame. Here the Chapter 10 groundwork
pays off completely: because the syscall frame is the *entire* user context laid
out as a struct, cloning execution state is a `memcpy` and a single field write.
The child returns 0, the parent returns the child's pid — same frame, one field
different.

**`execve`** (syscall 59) is the sharpest example of *build-new-then-commit* — a
transaction discipline you should reach for whenever an operation can fail
partway. It first copies the path and both string vectors *out* of user memory
(bounded: 16 args, 128 bytes of strings, path ≤ 63 — untrusted input gets hard
limits), then **builds the complete new image before touching the old one**: new
address space, ELF segments loaded, stack constructed, argv/envp marshalled. Only
once everything has succeeded does it commit — swap the address space, reload
`CR3`, destroy the old space, and rewrite the saved syscall frame so `sysret`
lands at the new entry point. The consequence: any failure (`-ENOENT`,
`-ENOMEM`, `-E2BIG`) returns with the calling process **completely unharmed**,
still running its old image. A half-replaced process is not a state that can
exist. If you take one design pattern from this chapter, take this: when an
operation mutates precious state and can fail, construct the entire replacement
off to the side and swap it in with one commit, so failure is always a clean
no-op.

**`exit`** (syscall 60) marks the process a zombie holding its status, reparents
its children to init, wakes the parent if it is blocked in `wait4`, and ends the
hosting thread.

**`wait4`** (syscall 61) finds a matching child (exact pid, or `-1` for any). A
zombie is reaped — join the hosting thread, destroy the address space, free the
table slot, and deliver the Linux wait status (`(code & 0xff) << 8` for a normal
exit; the signal number for a fault kill — real `WIFEXITED`/`WIFSIGNALED`
encoding). Otherwise the parent publishes itself as the waiter and blocks. The
critical detail: **the check for an existing zombie and the decision to block
happen in one interrupts-off section**, so a child exiting concurrently on
another thread cannot slip through the gap between "I looked and saw no zombie"
and "I went to sleep" — the classic *lost wakeup*. This is the single most common
concurrency bug in wait/notify code, and the fix is always the same shape: the
check and the sleep must be atomic with respect to the notify. Chapter 9's
interrupts-off lock makes that atomicity a two-line bracket.

## 11.4 Dying alone: fault isolation as the payoff

Back in Chapter 4 the trap dispatcher was built to check *who was running* when a
fault hit. Now that pays off. A hardware exception raised in **ring 3** is never
the kernel's problem:

> The dispatcher logs one diagnostic line (exception, `rip`, error code, `cr2`
> for page faults) and **kills the offending process** with the Linux signal for
> that exception — `#PF`/`#GP` → `SIGSEGV`, `#UD` → `SIGILL`, `#DE`/`#MF`/`#XM`
> → `SIGFPE` — delivered to the parent through `wait4`. The kernel and every
> other process keep running.

Only machine-level events (NMI, double fault, machine check) and *any* fault
taken in kernel mode still panic — a kernel-mode fault is a kernel bug by
definition, and there is nothing safe to do but stop. And if *init itself* dies
by signal, the kernel panics, Unix-style, because the system has lost its root
process.

This is isolation demonstrated, not asserted. Init deliberately forks children
that misbehave — one writes to a kernel address (killed with `SIGSEGV`), one
executes an illegal instruction (killed with `SIGILL`) — and reaps both through
`wait4` while everything else keeps running. The boot log shows the two fault
diagnostics sandwiched between normal output, and the run continues to
`boot: complete`. A crash that the rest of the system shrugs off, on every boot,
in the automated test.

## 11.5 Init as the acceptance test

Init (`user/init.c` plus `user/crt0.asm`) is not just the first process; it is
**the acceptance test for the entire userspace stack**, and its exit status names
the first failed check — 0 means all twenty passed. In one program it verifies:
`write` returns the full length; `.bss` is zero-filled and `.data` is initialized
and writable (proving the loader's segment handling); `getpid`; the `-ENOSYS`,
`-EBADF`, `-EFAULT` error paths; all six argument registers surviving a syscall
(proving the entry stub's register discipline from Chapter 10); the full process
round trip — fork returns a fresh pid, the child execve's `/bin/hello` which
checks its own argv arrived intact and exits 42, `wait4` returns that pid with
status `42 << 8`, a second `wait4` returns `-ECHILD`, `execve` of an unknown path
returns `-ENOENT`; and fault isolation itself. After init is reaped, the kernel
asserts the process table is empty and the physical frame count exactly matches
the pre-launch value: **the whole fork/exec/wait cycle leaked nothing.**

Making your first userspace program a comprehensive self-check of the layer
beneath it is a wonderful bootstrapping move. Init has to exist anyway; giving it
the job of exercising every syscall, every error path, and every leak invariant
means the most fundamental layer of the system is re-verified on every single
boot, by the very component that depends on it most.

## 11.6 The transferable lessons

- **Separate total validation from action.** A pure, exhaustively-tested
  validator establishes an invariant; the loader acts under it without defensive
  re-checking. Prove the validator is *total* with one-mutation-per-path tests.
- **Build-new-then-commit for fallible mutations.** `execve` constructs the whole
  new process off to the side and swaps it in atomically, so any failure leaves
  the caller untouched. A half-done state never exists.
- **Make check-and-block atomic to kill lost wakeups.** `wait4` looks for a
  zombie and goes to sleep in one interrupts-off section, closing the gap a
  concurrent exit could slip through.
- **Build the process on the thread, not beside it.** Hosting each process in a
  kernel thread inherits preemption, sleep, and join for free.
- **Let your first program test the layer under it.** Init doubles as the
  userspace acceptance suite, re-run on every boot.

The system now runs isolated C programs that start, fork, exec, crash safely, and
are reaped — but they still come from a blob embedded in the kernel, because
there is no disk to load them from. The last two subsystem chapters give the
kernel real storage and a real filesystem.
