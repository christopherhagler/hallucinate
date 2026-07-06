/*
 * test_sched.c - unit tests for the scheduler policy core.
 *
 * Exercises kernel/sched/sched_core.c: FIFO ready-queue ordering, the
 * wake-tick-sorted sleep list, and a randomized stress run checked
 * against a shadow model.
 */
#include "test.h"

#include <sched_core.h>

TEST(sched_ready_queue_fifo) {
    struct sched_core sc;
    sched_core_init(&sc);

    ASSERT_TRUE(!sched_core_has_ready(&sc));
    ASSERT_TRUE(sched_core_dequeue(&sc) == NULL);

    struct thread t[4] = {0};
    for (int i = 0; i < 4; i++) {
        t[i].id = (uint64_t)i;
        sched_core_enqueue(&sc, &t[i]);
        ASSERT_EQ_INT(THREAD_READY, t[i].state);
    }
    ASSERT_TRUE(sched_core_has_ready(&sc));

    for (int i = 0; i < 4; i++) {
        struct thread *got = sched_core_dequeue(&sc);
        ASSERT_TRUE(got == &t[i]);
    }
    ASSERT_TRUE(!sched_core_has_ready(&sc));
    ASSERT_TRUE(sched_core_dequeue(&sc) == NULL);

    /* Re-enqueueing after drain must relink head and tail correctly. */
    sched_core_enqueue(&sc, &t[2]);
    sched_core_enqueue(&sc, &t[0]);
    ASSERT_TRUE(sched_core_dequeue(&sc) == &t[2]);
    ASSERT_TRUE(sched_core_dequeue(&sc) == &t[0]);
    ASSERT_TRUE(sched_core_dequeue(&sc) == NULL);
}

TEST(sched_sleep_wakes_in_deadline_order) {
    struct sched_core sc;
    sched_core_init(&sc);

    struct thread t[3] = {0};
    sched_core_sleep(&sc, &t[0], 30);
    sched_core_sleep(&sc, &t[1], 10);
    sched_core_sleep(&sc, &t[2], 20);
    for (int i = 0; i < 3; i++) {
        ASSERT_EQ_INT(THREAD_SLEEPING, t[i].state);
    }

    /* Nothing due yet. */
    ASSERT_EQ_INT(0, sched_core_wake_due(&sc, 9));
    ASSERT_TRUE(!sched_core_has_ready(&sc));

    /* Exactly-due semantics: wake_tick <= now. */
    ASSERT_EQ_INT(1, sched_core_wake_due(&sc, 10));
    ASSERT_TRUE(sched_core_dequeue(&sc) == &t[1]);
    ASSERT_EQ_INT(THREAD_READY, t[1].state);

    /* A single call wakes everything due, in deadline order. */
    ASSERT_EQ_INT(2, sched_core_wake_due(&sc, 100));
    ASSERT_TRUE(sched_core_dequeue(&sc) == &t[2]);
    ASSERT_TRUE(sched_core_dequeue(&sc) == &t[0]);
    ASSERT_EQ_INT(0, sched_core_wake_due(&sc, 1000));
}

TEST(sched_sleep_equal_deadlines_keep_fifo_order) {
    struct sched_core sc;
    sched_core_init(&sc);

    struct thread t[4] = {0};
    sched_core_sleep(&sc, &t[0], 5);
    sched_core_sleep(&sc, &t[1], 5);
    sched_core_sleep(&sc, &t[2], 4);
    sched_core_sleep(&sc, &t[3], 5);

    ASSERT_EQ_INT(4, sched_core_wake_due(&sc, 5));
    ASSERT_TRUE(sched_core_dequeue(&sc) == &t[2]);
    ASSERT_TRUE(sched_core_dequeue(&sc) == &t[0]);
    ASSERT_TRUE(sched_core_dequeue(&sc) == &t[1]);
    ASSERT_TRUE(sched_core_dequeue(&sc) == &t[3]);
}

/* Deterministic xorshift64 so failures reproduce. */
static uint64_t rng_state = 0x5DEECE66DULL;
static uint64_t rng(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return rng_state;
}

/*
 * Stress: drive random enqueue/sleep/dequeue/tick traffic and mirror
 * it in a shadow model (arrays instead of intrusive lists). The core
 * must agree with the shadow on every dequeue and wake count.
 */
enum { STRESS_THREADS = 64, STRESS_ROUNDS = 20000 };

static struct thread pool[STRESS_THREADS];

/* Shadow model: FIFO of thread indices + per-thread deadline table.
 * shadow_seq records sleep-insertion order for FIFO-among-equal-
 * deadline tie-breaking, matching the core's documented behavior. */
static int shadow_ready[STRESS_THREADS * 2];
static int shadow_ready_len;
static uint64_t shadow_deadline[STRESS_THREADS]; /* 0 = not sleeping */
static uint64_t shadow_seq[STRESS_THREADS];
static uint64_t shadow_seq_next;

static int shadow_sleeps_before(int a, int b) {
    if (shadow_deadline[a] != shadow_deadline[b]) {
        return shadow_deadline[a] < shadow_deadline[b];
    }
    return shadow_seq[a] < shadow_seq[b];
}

static void shadow_push_ready(int idx) {
    shadow_ready[shadow_ready_len++] = idx;
}

static int shadow_pop_ready(void) {
    if (shadow_ready_len == 0) {
        return -1;
    }
    int idx = shadow_ready[0];
    memmove(&shadow_ready[0], &shadow_ready[1], (size_t)(shadow_ready_len - 1) * sizeof(int));
    shadow_ready_len--;
    return idx;
}

TEST(sched_randomized_stress_vs_shadow_model) {
    struct sched_core sc;
    sched_core_init(&sc);

    /* idle[i]: thread i is neither ready nor sleeping in either model. */
    int idle[STRESS_THREADS];
    for (int i = 0; i < STRESS_THREADS; i++) {
        pool[i].id = (uint64_t)i;
        idle[i] = 1;
        shadow_deadline[i] = 0;
    }
    shadow_ready_len = 0;
    uint64_t now = 1;

    for (int round = 0; round < STRESS_ROUNDS; round++) {
        switch (rng() % 4) {
        case 0: { /* enqueue an idle thread */
            int i = (int)(rng() % STRESS_THREADS);
            if (idle[i]) {
                idle[i] = 0;
                sched_core_enqueue(&sc, &pool[i]);
                shadow_push_ready(i);
            }
            break;
        }
        case 1: { /* put an idle thread to sleep */
            int i = (int)(rng() % STRESS_THREADS);
            if (idle[i]) {
                idle[i] = 0;
                uint64_t deadline = now + 1 + (rng() % 50);
                sched_core_sleep(&sc, &pool[i], deadline);
                shadow_deadline[i] = deadline;
                shadow_seq[i] = shadow_seq_next++;
            }
            break;
        }
        case 2: { /* dequeue; both models must agree exactly */
            struct thread *got = sched_core_dequeue(&sc);
            int want = shadow_pop_ready();
            if (want < 0) {
                ASSERT_TRUE(got == NULL);
            } else {
                ASSERT_TRUE(got == &pool[want]);
                idle[want] = 1;
            }
            break;
        }
        default: { /* advance time and wake sleepers */
            now += rng() % 8;
            unsigned woken = sched_core_wake_due(&sc, now);
            /* Shadow: wake in (deadline, insertion) order via
             * repeated selection of the earliest due sleeper. */
            unsigned shadow_woken = 0;
            for (;;) {
                int best = -1;
                for (int i = 0; i < STRESS_THREADS; i++) {
                    if (shadow_deadline[i] != 0 && shadow_deadline[i] <= now &&
                        (best < 0 || shadow_sleeps_before(i, best))) {
                        best = i;
                    }
                }
                if (best < 0) {
                    break;
                }
                shadow_deadline[best] = 0;
                shadow_push_ready(best);
                shadow_woken++;
            }
            ASSERT_EQ_INT(shadow_woken, woken);
            break;
        }
        }
    }

    /* Drain both models; the full remaining order must match. */
    (void)sched_core_wake_due(&sc, now + 1000);
    for (;;) {
        int best = -1;
        for (int i = 0; i < STRESS_THREADS; i++) {
            if (shadow_deadline[i] != 0 && (best < 0 || shadow_sleeps_before(i, best))) {
                best = i;
            }
        }
        if (best < 0) {
            break;
        }
        shadow_deadline[best] = 0;
        shadow_push_ready(best);
    }
    for (;;) {
        struct thread *got = sched_core_dequeue(&sc);
        int want = shadow_pop_ready();
        if (want < 0) {
            ASSERT_TRUE(got == NULL);
            break;
        }
        ASSERT_TRUE(got == &pool[want]);
    }
}
