/*
 * sched_core.h - scheduler policy core.
 *
 * Pure bookkeeping over struct thread intrusive lists: a FIFO ready
 * queue (round-robin order) and a wake-tick-sorted sleep list. No
 * arch code, no locking, no panics — callers serialize access — so
 * the identical code is unit-tested on the host under sanitizers
 * (tests/host/test_sched.c). The kernel wrapper (kernel/sched/sched.c)
 * adds interrupt-off critical sections, stacks, and context switching.
 */
#pragma once

#include <stdint.h>

#include <thread.h>

struct sched_core {
    struct thread *ready_head; /* FIFO ready queue: dequeue here... */
    struct thread *ready_tail; /* ...enqueue here */
    struct thread *sleepers;   /* ascending wake_tick; FIFO among equals */
};

void sched_core_init(struct sched_core *sc);

/* Append a thread to the ready queue tail and mark it READY. */
void sched_core_enqueue(struct sched_core *sc, struct thread *t);

/* Pop the ready queue head, or NULL if no thread is ready. The
 * returned thread's state is left READY; the caller marks it RUNNING. */
struct thread *sched_core_dequeue(struct sched_core *sc);

/* 1 if at least one thread is ready to run. */
int sched_core_has_ready(const struct sched_core *sc);

/* Insert a thread into the sleep list, to wake at absolute tick
 * `wake_tick`, and mark it SLEEPING. */
void sched_core_sleep(struct sched_core *sc, struct thread *t, uint64_t wake_tick);

/* Move every sleeper with wake_tick <= now onto the ready queue
 * (in wake order). Returns the number of threads woken. */
unsigned sched_core_wake_due(struct sched_core *sc, uint64_t now);
