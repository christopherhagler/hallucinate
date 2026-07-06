/*
 * sched_core.c - scheduler policy core (see sched_core.h).
 */
#include <sched_core.h>

#include <stddef.h>

void sched_core_init(struct sched_core *sc) {
    sc->ready_head = NULL;
    sc->ready_tail = NULL;
    sc->sleepers = NULL;
}

void sched_core_enqueue(struct sched_core *sc, struct thread *t) {
    t->state = THREAD_READY;
    t->next = NULL;
    if (sc->ready_tail != NULL) {
        sc->ready_tail->next = t;
    } else {
        sc->ready_head = t;
    }
    sc->ready_tail = t;
}

struct thread *sched_core_dequeue(struct sched_core *sc) {
    struct thread *t = sc->ready_head;
    if (t == NULL) {
        return NULL;
    }
    sc->ready_head = t->next;
    if (sc->ready_head == NULL) {
        sc->ready_tail = NULL;
    }
    t->next = NULL;
    return t;
}

int sched_core_has_ready(const struct sched_core *sc) {
    return sc->ready_head != NULL;
}

void sched_core_sleep(struct sched_core *sc, struct thread *t, uint64_t wake_tick) {
    t->state = THREAD_SLEEPING;
    t->wake_tick = wake_tick;

    /* Insert in ascending wake_tick order; strictly-greater comparison
     * keeps FIFO order among sleepers sharing a wake tick. */
    struct thread **link = &sc->sleepers;
    while (*link != NULL && (*link)->wake_tick <= wake_tick) {
        link = &(*link)->next;
    }
    t->next = *link;
    *link = t;
}

unsigned sched_core_wake_due(struct sched_core *sc, uint64_t now) {
    unsigned woken = 0;
    while (sc->sleepers != NULL && sc->sleepers->wake_tick <= now) {
        struct thread *t = sc->sleepers;
        sc->sleepers = t->next;
        sched_core_enqueue(sc, t);
        woken++;
    }
    return woken;
}
