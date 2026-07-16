# Chapter 9 — Threads and Scheduling

So far the kernel is one thread of control: `kmain` runs top to bottom. A
multitasking OS needs many independent flows — kernel worker threads now, and the
kernel-side halves of userspace processes soon — that share the CPU. This chapter
builds that: what a thread *is*, how you switch between two of them, how the
timer forcibly preempts one, and the handful of invariants that keep the whole
thing from eating itself. It is the most concurrency-subtle chapter in the book,
and the payoff is that Chapter 11's processes get preemption, sleep, and join for
free.

## 9.1 A thread is a stack (and a little bookkeeping)

Strip away the abstraction and a thread is remarkably concrete: **a stack, plus
enough saved state to resume it.** Each thread here gets a 16 KiB `kmalloc`'d
stack and a `struct thread` control block holding its state, its saved stack
pointer, and a single `next` link for whatever list it is on. The boot context —
`kmain` itself, on the boot stack from `entry.asm` — is *adopted* as thread 0,
"main." It was never created; it simply gets a control block so the scheduler can
manage the flow that was already running. (Recall `entry.asm` marked
`kstack_top` global "the scheduler adopts this as thread 0's stack" — this is the
payoff.)

The states a thread moves through (Appendix G):

- **READY** — runnable, sitting on the ready queue.
- **RUNNING** — on the CPU; at most one at a time.
- **SLEEPING** — on a wake-tick-sorted sleep list, waiting for a timer deadline.
- **BLOCKED** — parked waiting for an event (`thread_join`, or the generic
  `sched_block()`/`sched_wake()` pair that `wait4` will block on).
- **ZOMBIE** — exited, its control block and stack awaiting reclamation by
  whoever joins it.

## 9.2 The context switch: the trick at the center of it all

Here is the single most illuminating piece of code in the kernel, the entire
context switch (`kernel/arch/x86_64/ctx.asm`):

```asm
; void ctx_switch(uint64_t *save_rsp, uint64_t load_rsp)
global ctx_switch
ctx_switch:
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15
    mov [rdi], rsp      ; park the outgoing thread
    mov rsp, rsi        ; adopt the incoming thread's stack
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    ret
```

Sit with how little this is. A thread that is not running is *entirely captured
by its saved `rsp`* — because everything that defines where it was is already on
its own stack. `ctx_switch` pushes the System V **callee-saved** registers
(`rbp rbx r12–r15`) onto the outgoing stack, saves that stack pointer into the
outgoing thread's control block, loads the incoming thread's stack pointer, pops
*its* callee-saved registers back, and `ret`s — into whatever the incoming thread
was doing when *it* last called `ctx_switch`. The two threads swap identities at
the `mov rsp, rsi`.

Why only the callee-saved registers? Because `ctx_switch` is an **ordinary C
function call**. The System V ABI says a function call may clobber the
caller-saved registers, so the compiler *already spilled* anything live across
the call at the call site. `ctx_switch` only needs to preserve what the ABI says
a function must preserve. This is a beautiful example of *using the ABI as a
tool*: half the register-saving work is delegated to the compiler for free,
simply because you made the switch look like a normal call. Understanding this is
a genuine level-up — it is the moment context switching stops being magic.

There are two hard constraints, both commented in the file. First, **interrupts
must be off** across the switch: a timer interrupt landing between `mov rsp, rsi`
and the `ret` would run a handler on a half-switched thread with an inconsistent
register set. Second, a **brand-new thread has no saved frame to `ret` into**, so
the scheduler hand-builds a fake one whose return address is
`thread_entry_trampoline`, with the entry function in `r12` and its argument in
`r13`:

```asm
thread_entry_trampoline:
    sti                 ; interrupts were off (scheduler invariant); turn them on
    mov rdi, r13        ; argument
    call r12            ; entry(argument); rsp is 16-aligned here
    call thread_exit    ; falling off the entry function ends the thread cleanly
```

The trampoline is placed 8 bytes below the 16-byte-aligned stack top so that
`rsp` is 16-aligned *at the call sites*, as the ABI requires — get that wrong and
the first function that uses an aligned SSE spill (or just the ABI's alignment
assumption) crashes in a way that looks unrelated to threading. And note the
`sti`: a new thread is entered through `ctx_switch`'s `ret` with interrupts off
(the invariant), so the trampoline re-enables them before running the thread
body. Every thread's first instruction has to undo the interrupts-off state the
switch guaranteed. Falling off the end of the entry function lands in
`thread_exit`, so a thread that simply returns terminates cleanly — no special
case required.

## 9.3 Preemption: taking the CPU by force

Cooperative scheduling — threads yielding voluntarily — is not enough; one
thread with an infinite loop would freeze the system. Preemption uses the 100 Hz
timer from Chapter 5. The tick handler calls the scheduler's registered hook,
which wakes any due sleepers and sets a `need_resched` flag whenever another
thread is ready. The timeslice is one tick, 10 ms.

The subtle and correct part is *when* the switch happens. The tick handler does
**not** call `schedule()` inline. Instead the IRQ dispatcher performs the switch
at interrupt exit, **after the EOI** (`sched_preempt()`), so the in-service
interrupt is fully retired before another thread runs — otherwise the PIC would
think that IRQ is still being serviced and could stall further ticks. The
preempted thread's interrupt frame simply stays parked on its own kernel stack;
when it is scheduled back in, control returns up through the interrupt path and
`iretq`s as if nothing happened. The thread never knows it was preempted. That
transparency — a thread cannot tell the difference between running straight
through and being frozen for a scheduling quantum — *is* preemptive multitasking.

The same mechanism handles wakeups from interrupt context: making a thread READY
inside a handler flags a reschedule that fires at IRQ exit, so a thread woken by
(say) a keyboard interrupt runs within the same tick rather than waiting out the
current timeslice. Low wakeup latency, for free, from the same "switch at IRQ
exit" rule.

## 9.4 The invariants, and why each one is a specific catastrophe

Concurrency bugs are the hardest to reproduce and the most expensive to ship, so
this subsystem is governed by five invariants (from Chapter 1, now with their
teeth showing). For each, name the disaster it prevents:

1. **`schedule()` is entered with interrupts disabled — always (asserted).**
   Violate it and a switch races a timer tick, running a handler on a
   half-switched thread.
2. **The running thread is never on the ready queue or the sleep list.** Violate
   it and the thread gets scheduled onto a second CPU-time slot or "woken" while
   already running — double-scheduling, instant corruption.
3. **The idle thread never blocks, sleeps, exits, or enters the ready queue.** It
   is the fallback when everyone else is blocked, and it `hlt`s. Violate it and
   the scheduler can find *nothing* to run and dereferences a null "next."
4. **A thread is on at most one list at a time** (`next` is its only link).
   Violate it and the same thread appears on two lists, and one list's operations
   corrupt the other's.
5. **A zombie's stack is freed only from `thread_join`, which can only run after
   the zombie's final `ctx_switch` has left that stack for good.** Violate it and
   you free a stack a thread is still standing on — a use-after-free of live
   execution state, the worst bug in the chapter.

Invariant 5 is worth dwelling on because it is a genuine ordering hazard. A
thread exits by becoming a zombie and switching away *one last time*. Its stack
is still in use right up to the instant of that final `ctx_switch` — the switch
itself runs on it. Only after control has left for good is the stack safe to
free, and the one place guaranteed to run after that is the *joiner*. So freeing
is the joiner's job, never the exiting thread's. Whenever a resource's last user
is the very code releasing it, you have this hazard; the resolution is always to
move the release to some other party that provably runs strictly afterward.

## 9.5 Proving it interleaves

True to form, none of this is trusted — it is demonstrated on every boot. Three
worker threads each log a tag and yield, four rounds; FIFO round-robin makes the
serial output a perfect rotation:

```
selftest: sched interleave "abcabcabcabc"
```

Any scheduling bug perturbs that exact string, and the QEMU integration test
asserts it. The suite also proves timed sleep blocks for the requested number of
ticks, that a thread spinning without yielding cannot monopolize the CPU (the
sleeping main thread gets it back *via preemption* — proving preemption works,
not just cooperation), and that join reclaims every byte. A deterministic
output string as a concurrency oracle is a lovely technique: make the correct
interleaving produce one specific, fragile artifact, and assert on it.

## 9.6 The transferable lessons

- **A suspended thread is just a saved stack pointer.** Everything else is on its
  stack; the switch is a stack swap bracketed by register saves.
- **Use the ABI as a tool.** Making the context switch a normal C call lets the
  compiler save the caller-saved registers for you — half the work, for free.
- **Preempt at interrupt exit, not inline.** Retire the interrupt first, then
  switch; the preempted thread returns through the interrupt path none the wiser.
- **When the last user of a resource is the code releasing it, hand the release
  to someone who runs strictly later.** That is why joiners free zombie stacks.
- **Turn correct concurrency into a deterministic artifact and assert on it.**
  `"abcabcabcabc"` is a scheduling oracle.

The kernel is now a real multitasking system — but everything still runs in ring
0, fully privileged, sharing one address space. The next chapter crosses the
biggest boundary in the book: down to ring 3, into isolated user address spaces,
and back up through a system call.
