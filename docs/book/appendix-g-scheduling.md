# Threads and Scheduling

Phase 3 gives the kernel preemptive multitasking: kernel threads with their own
stacks, a round-robin scheduler driven by the timer tick, timed sleep, and
pthread-style join. Userspace processes (Phase 4) will build their kernel-side
execution on these threads.

## Layering

| Layer | File | Tested by |
|---|---|---|
| Policy (ready queue, sleep list) | `kernel/sched/sched_core.c` | host unit tests, ASan/UBSan |
| Mechanism (stacks, switch, timer, blocking) | `kernel/sched/sched.c` | in-kernel selftests + QEMU markers |
| Context switch | `kernel/arch/x86_64/ctx.asm` | in-kernel selftests |

`sched_core.c` is pure list manipulation over `struct thread` and never touches
hardware, locks, or allocation, so the identical code runs under sanitizers on
the host (`tests/host/test_sched.c`), including a 20,000-round randomized
stress run checked against a shadow model.

## Thread model

- Every thread is a kernel thread with a 16 KiB `kmalloc`'d stack (the boot
  context is adopted as thread 0, "main", on the boot stack; guard pages come
  with per-process address spaces in Phase 4).
- States: `READY` (on the ready queue), `RUNNING` (at most one), `SLEEPING`
  (on the wake-tick-sorted sleep list), `BLOCKED` (waiting in `thread_join`
  or parked via `sched_block()`/`sched_wake()` — the primitive `wait4` blocks
  on), `ZOMBIE` (exited, awaiting join).
- Threads are joinable, pthread-style: each created thread must be passed to
  `thread_join()` exactly once; join blocks until the thread exits, then frees
  its stack and control block. The selftests assert the heap returns to its
  pre-test object count, so leaks in this path cannot land.
- Returning from the entry function is equivalent to calling `thread_exit()`.

## Context switch

A thread off the CPU is exactly its saved `rsp`. `ctx_switch(&old->rsp,
new->rsp)` pushes the System V callee-saved registers (`rbp rbx r12-r15`) onto
the outgoing stack, swaps stack pointers, pops the incoming thread's registers,
and `ret`s. Caller-saved registers need no treatment: `ctx_switch` is an
ordinary function call, so the compiler already saved what mattered.

New threads get a hand-built frame whose return address is
`thread_entry_trampoline` with `r12` = entry function and `r13` = argument.
The trampoline is placed 8 bytes below the 16-byte-aligned stack top so `rsp`
is 16-aligned at its call sites, as the ABI requires. It enables interrupts,
calls `entry(arg)`, and falls into `thread_exit()`.

## Preemption

The 100 Hz PIT tick calls the scheduler's tick hook (registered via
`timer_set_tick_hook`), which wakes due sleepers and flags `need_resched`
whenever any thread is ready — the timeslice is one tick, 10 ms. The IRQ
dispatcher performs the actual switch *after* sending EOI (`sched_preempt()`),
so the in-service interrupt never blocks further ticks. The preempted thread's
trapframe simply stays parked on its own kernel stack; when scheduled back in,
it returns up through the interrupt path and `iretq` as if nothing happened.

Wakeups from interrupt handlers get the same treatment: making a thread ready
in interrupt context flags a reschedule that happens at IRQ exit, so a woken
thread runs within the same tick, not after the current timeslice expires.

## Invariants

1. `schedule()` is entered with interrupts disabled — always (asserted).
2. The running thread is never on the ready queue or the sleep list.
3. The idle thread never blocks, sleeps, exits, or enters the ready queue; it
   is the fallback when everyone else is blocked, and it `hlt`s.
4. A thread is on at most one list at a time (`next` is the only link).
5. A zombie's stack is freed only from `thread_join`, which can only run after
   the zombie's final `ctx_switch` has left that stack for good.

## Interrupt-off critical sections

One CPU, so disabling interrupts is the kernel lock. With preemption, any
state shared between threads needs it: the scheduler's own lists, the heap
(`kmalloc`/`kfree`), the frame allocator, and `kprintf`'s output emission
(formatting happens outside the critical section; only the console write is
atomic, keeping log lines whole). `cpu_irq_save()`/`cpu_irq_restore()` nest
correctly, so an allocator calling another allocator stays safe. These
sections become real spinlocks when SMP arrives.

## Proof of interleaving

Every boot runs a scheduler selftest whose result prints to serial and is
asserted by the QEMU integration test:

```
sched: online, round-robin, 10 ms timeslice
selftest: sched interleave "abcabcabcabc"
```

Three workers each log their tag and yield, four rounds; FIFO round-robin makes
the log a perfect rotation. The suite also proves timed sleep blocks for the
requested ticks, that a spinning thread which never yields cannot monopolize
the CPU (the sleeping main thread gets it back via preemption), and that join
reclaims every byte.
