/*
 * mutex.h - sleeping mutual exclusion.
 *
 * A mutex serializes thread-context code that can sleep — file
 * system and block I/O paths above the polling drivers. Contenders
 * block (scheduler-level, not spinning) in FIFO order; the waiter
 * queue reuses thread->next, which a THREAD_BLOCKED thread is not
 * using (it sits on no scheduler list until woken).
 *
 * Rules: thread context only (never an IRQ handler), non-recursive
 * (relock by the owner asserts), and unlock only by the owner.
 * Single CPU: the fields themselves are protected by cpu_irq_save
 * sections inside lock/unlock.
 */
#pragma once

struct thread;

struct mutex {
    struct thread *owner;     /* NULL = unlocked */
    struct thread *wait_head; /* FIFO of blocked contenders */
    struct thread *wait_tail;
    const char *name; /* for diagnostics */
};

/* Static initializer, for file-scope locks. */
#define MUTEX_INITIALIZER(nm) {0, 0, 0, (nm)}

void mutex_init(struct mutex *m, const char *name);

/* Acquire, blocking until available. */
void mutex_lock(struct mutex *m);

/* Release; hands the lock to the oldest waiter if one exists. */
void mutex_unlock(struct mutex *m);

/* Nonzero when the calling thread holds `m` (for assertions). */
int mutex_held(const struct mutex *m);
