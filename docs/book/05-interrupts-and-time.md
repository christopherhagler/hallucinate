# Chapter 5 — Interrupts and Time

An exception is *synchronous* — it is caused by the instruction currently
executing (a divide by zero, a bad memory access). A hardware interrupt is
*asynchronous* — the timer or the keyboard raises a line whenever it feels like
it, with no relationship to what the CPU is doing. Handling asynchrony correctly
is where a program becomes a system, and it is also where a whole new category of
bugs — races, lost wakeups, reentrancy — is born. This chapter covers the
legacy 8259 PIC, the 8254 timer that gives the kernel its heartbeat, the
keyboard, and the discipline of doing as little as possible in interrupt context.

## 5.1 The PIC, and why you remap it before you dare enable it

The 8259 Programmable Interrupt Controller is the legacy chip (a pair of them,
cascaded) that multiplexes hardware IRQ lines onto CPU interrupt vectors. There
is exactly one thing you must know before turning interrupts on: by default the
PIC delivers IRQs 0–15 on vectors **0x08–0x0F and 0x70–0x77**, and vectors
0x08–0x0F *collide with CPU exceptions* (0x08 is `#DF`, the double fault; 0x0D is
`#GP`). If you enable interrupts without remapping, the first timer tick arrives
as vector 0x08 and the kernel processes a hardware timer interrupt as a double
fault. This is a rite-of-passage bug for OS beginners.

So `pic_init()` remaps the two PICs to vectors **0x20–0x2F**, safely above the
32 architectural exception vectors, and then **masks everything**. Bring-up
order matters: the PIC is remapped and fully masked in step 2 of `kmain`, long
before interrupts are globally enabled in step 9. You unmask a line only when its
handler is installed and its device initialized. The general rule — turn a source
off until its consumer is ready — is why `cpu_enable_interrupts()` sits after
`timer_init()` and `keyboard_init()` and not before.

Every interrupt handler must also send an **End-Of-Interrupt** to the PIC, or
that line never fires again. Forgetting the EOI gives you a device that works
exactly once. The IRQ layer (`irq.c`) centralizes this so individual handlers
cannot forget.

## 5.2 The timer: the kernel's sense of time and its preemption engine

The 8254 PIT is programmed by `timer_init(100)` to fire IRQ 0 at **100 Hz** — one
tick every 10 ms. That single periodic interrupt does double duty:

1. It is the kernel's **clock**. `timer_ticks()` counts ticks; `timer_sleep_ticks()`
   blocks for a number of them. The boot sequence proves the timer actually
   advances by sleeping three ticks and checking the counter moved — a liveness
   assertion, not just an initialization.
2. It is the **preemption source**. The tick handler calls the scheduler's tick
   hook, which is what lets the kernel take the CPU away from a thread that would
   otherwise run forever. Without a timer interrupt there is no preemption, only
   cooperative yielding — the difference between a real multitasking kernel and a
   coroutine library. Chapter 9 details how the tick drives round-robin
   scheduling.

The design choice worth noting is *decoupling*: the timer driver does not know
the scheduler exists. It exposes `timer_set_tick_hook()`, and the scheduler
registers itself. The timer's job is "call this function 100 times a second"; the
policy of what that does lives entirely in the scheduler. This keeps the driver
reusable and testable and keeps a hard dependency from pointing the wrong way (a
device driver should never depend on a policy layer above it).

## 5.3 The keyboard, and the cardinal rule of interrupt handlers

The PS/2 keyboard raises IRQ 1 with a *scancode* — a hardware key-event code, not
an ASCII character. Translating scancodes to characters involves state (shift,
caps lock) and a lookup table, and that translation is pure logic with no
hardware in it. So, following the core/driver split from Chapter 1, the scancode
map is factored into `kernel/drivers/kbd_map.c` — a pure function, host-tested in
`tests/host/test_kbd.c`. The interrupt handler reads the scancode from the
hardware port; the pure code turns it into a character.

This points at the cardinal rule of interrupt handlers: **do the minimum in
interrupt context, defer the rest.** An interrupt handler runs with the
interrupted thread's work suspended and (often) further interrupts masked; time
spent in it is time stolen from everything else, and any state it touches races
with the code it interrupted. So the handler should read the hardware, stash the
raw event somewhere, wake whoever cares, send the EOI, and get out. The
character translation, the echo, the line editing — that is the consumer's job,
run later in normal context. `kmain`'s final loop is the consumer: it calls
`keyboard_getchar()`, and if there is nothing, `cpu_wait_for_interrupt()`
(`hlt`) until the next interrupt. Producer in interrupt context, consumer in
thread context, a buffer between them — the shape of nearly every driver you will
ever write.

## 5.4 Reentrancy and the interrupts-off lock

Asynchrony creates a problem that did not exist before: code can now be
interrupted *in the middle of touching shared state*. If the timer fires while
`kmalloc` is halfway through unlinking a slab free-list node, and the interrupt
handler (or a thread it wakes) calls `kmalloc`, the free list is corrupt.

On a uniprocessor, the fix is blunt and correct: the shared-state critical
sections run with interrupts disabled. As Chapter 1 introduced, disabling
interrupts *is* the kernel's lock here, because on one CPU nothing else can run
while interrupts are off. `cpu_irq_save()`/`cpu_irq_restore()` bracket these
regions and — importantly — **nest**: they save and restore the previous
interrupt state rather than unconditionally enabling, so an allocator that calls
another allocator, each taking "the lock," does not prematurely re-enable
interrupts on the inner unlock. Getting nesting right is the difference between a
lock that composes and one that opens a one-instruction window every time it is
used reentrantly.

There is a matching subtlety in how the interrupt path interacts with the
scheduler. When a tick or a wakeup decides a reschedule is needed, the actual
context switch happens **at interrupt exit, after the EOI** — not in the middle
of the handler — so the in-service interrupt is fully retired before another
thread runs. Chapter 9 covers this; flag it here as the reason the timer handler
does not simply call `schedule()` inline.

## 5.5 The transferable lessons

- **Never enable a source before its consumer exists.** Remap and mask the PIC
  first; unmask a line only when its handler and device are ready; enable
  interrupts globally only after everything that fires is wired up.
- **Interrupt handlers do the minimum and defer.** Read the hardware, stash the
  event, wake the waiter, EOI, return. All real work happens in thread context.
- **Point dependencies downward.** The timer driver knows nothing of the
  scheduler; the scheduler registers a hook. Drivers must not depend on the
  policy layers above them.
- **Reentrant locks must nest.** Save-and-restore, never unconditional-enable,
  or reentrant use punches a hole in your critical section.

The kernel can now keep time and respond to the world. But every subsystem from
here up — the heap, thread stacks, process address spaces — needs *memory*, and
so far the kernel has been squatting on whatever physical addresses the
bootloader happened to leave free. The next two chapters build real memory
management: first the physical frame allocator, then virtual memory.
