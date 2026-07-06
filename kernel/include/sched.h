/*
 * sched.h - kernel threads and the round-robin scheduler.
 *
 * Model: cooperative + preemptive kernel threads on one CPU. Every
 * thread has its own kernel stack; the timer tick preempts whichever
 * thread is running whenever another is ready (10 ms timeslice at the
 * boot HZ). Threads are joinable, pthread-style: each thread must be
 * passed to thread_join() exactly once, which blocks until it exits
 * and then frees its stack and control block.
 *
 * Design and invariants: docs/scheduling.md.
 */
#pragma once

#include <stdint.h>

#include <compiler.h>
#include <thread.h>

/*
 * Adopt the running boot context as thread 0 ("main"), create the
 * idle thread, and hook the timer tick for wakeups and preemption.
 * Requires kmalloc_init(); call before enabling interrupts.
 */
void sched_init(void);

/*
 * Create a thread executing entry(arg) on a fresh kernel stack and
 * place it on the ready queue. `name` is borrowed for the thread's
 * lifetime (use string literals). Panics on exhaustion — thread
 * creation failure at this layer means the kernel is out of memory.
 */
struct thread *thread_create(const char *name, void (*entry)(void *), void *arg);

/* The currently running thread. */
struct thread *thread_current(void);

/*
 * Bind the calling thread to an address space (NULL = back to the
 * kernel address space) and activate it. The scheduler keeps it
 * active whenever this thread runs, so the thread can then enter
 * ring 3 or dereference user pointers.
 */
void sched_set_addrspace(struct addrspace *as);

/* Yield the CPU, keeping the caller runnable. Returns when scheduled
 * again (immediately if no other thread is ready). */
void sched_yield(void);

/* Block the caller for at least `ticks` timer ticks / `ms` milliseconds
 * (rounded up to a whole tick, minimum one). */
void sched_sleep_ticks(uint64_t ticks);
void sched_sleep_ms(uint64_t ms);

/* Terminate the calling thread; wakes its joiner if one is waiting.
 * Returning from a thread's entry function calls this implicitly. */
NORETURN void thread_exit(void);

/* Wait until `t` exits, then free its stack and control block. Each
 * created thread must be joined exactly once; `t` is invalid after. */
void thread_join(struct thread *t);

/*
 * Called by the IRQ dispatcher after EOI, still interrupts-off: if a
 * tick or wakeup flagged a reschedule, switch threads before
 * returning to the interrupted context. No-op before sched_init().
 */
void sched_preempt(void);

/* Number of live threads (including main and idle), for diagnostics. */
uint64_t sched_thread_count(void);
