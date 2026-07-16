/*
 * mutex.c - sleeping mutex over sched_block/sched_wake.
 *
 * Lock handoff: unlock picks the oldest waiter, sets it as the new
 * owner, and wakes it — the lock is never observably free while a
 * queue exists, so a fresh contender cannot barge past a waiter
 * (FIFO fairness, no starvation). The waiter queue is dequeued
 * before sched_wake so thread->next is free again by the time the
 * scheduler enqueues the thread.
 */
#include <mutex.h>

#include <stddef.h>

#include <arch/x86_64/cpu.h>
#include <panic.h>
#include <sched.h>

void mutex_init(struct mutex *m, const char *name) {
    m->owner = NULL;
    m->wait_head = NULL;
    m->wait_tail = NULL;
    m->name = name;
}

void mutex_lock(struct mutex *m) {
    struct thread *me = thread_current();
    uint64_t flags = cpu_irq_save();
    KASSERT(m->owner != me); /* non-recursive */
    if (m->owner == NULL) {
        m->owner = me;
        cpu_irq_restore(flags);
        return;
    }
    /* Contended: queue in FIFO order and block. Handoff in unlock
     * makes us the owner before the wakeup, so no retry loop. */
    me->next = NULL;
    if (m->wait_tail != NULL) {
        m->wait_tail->next = me;
    } else {
        m->wait_head = me;
    }
    m->wait_tail = me;
    sched_block();
    KASSERT(m->owner == me);
    cpu_irq_restore(flags);
}

void mutex_unlock(struct mutex *m) {
    uint64_t flags = cpu_irq_save();
    KASSERT(m->owner == thread_current());
    struct thread *next = m->wait_head;
    if (next != NULL) {
        m->wait_head = next->next;
        if (m->wait_head == NULL) {
            m->wait_tail = NULL;
        }
        next->next = NULL;
        m->owner = next; /* handoff: still locked, new owner */
        sched_wake(next);
    } else {
        m->owner = NULL;
    }
    cpu_irq_restore(flags);
}

int mutex_held(const struct mutex *m) {
    return m->owner == thread_current();
}
