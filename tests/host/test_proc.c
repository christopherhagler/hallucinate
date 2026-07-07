/* test_proc.c - process table state machine: pids, parents, zombies,
 * wait matching, reparenting, slot reuse. */
#include <proc_core.h>

#include "test.h"

TEST(proc_init_gets_pid_one) {
    struct proc_table pt;
    proc_table_init(&pt);
    ASSERT_EQ_INT(0, proc_count(&pt));

    ASSERT_EQ_INT(1, proc_alloc(&pt, 0)); /* init */
    ASSERT_EQ_INT(2, proc_alloc(&pt, 1));
    ASSERT_EQ_INT(3, proc_alloc(&pt, 1));
    ASSERT_EQ_INT(3, proc_count(&pt));

    struct proc_entry *p = proc_find(&pt, 2);
    ASSERT_TRUE(p != NULL);
    ASSERT_EQ_INT(1, p->ppid);
    ASSERT_EQ_INT(PROC_LIVE, p->state);

    ASSERT_TRUE(proc_find(&pt, 99) == NULL);
    ASSERT_TRUE(proc_find(&pt, 0) == NULL);
    ASSERT_TRUE(proc_find(&pt, -1) == NULL);
}

TEST(proc_table_fills_and_reuses_slots) {
    struct proc_table pt;
    proc_table_init(&pt);
    ASSERT_EQ_INT(1, proc_alloc(&pt, 0));
    for (int i = 1; i < PROC_MAX; i++) {
        ASSERT_TRUE(proc_alloc(&pt, 1) > 0);
    }
    ASSERT_EQ_INT(PROC_MAX, proc_count(&pt));
    ASSERT_EQ_INT(0, proc_alloc(&pt, 1)); /* full */

    /* Exit + reap frees the slot; the pid is never reused. */
    ASSERT_EQ_INT(1, proc_exit(&pt, 2, 0));
    proc_reap(&pt, 2);
    ASSERT_EQ_INT(PROC_MAX - 1, proc_count(&pt));
    int fresh = proc_alloc(&pt, 1);
    ASSERT_EQ_INT(PROC_MAX + 1, fresh);
    ASSERT_TRUE(proc_find(&pt, 2) == NULL);
}

TEST(proc_exit_and_wait_exact_pid) {
    struct proc_table pt;
    proc_table_init(&pt);
    proc_alloc(&pt, 0);         /* 1: init */
    int a = proc_alloc(&pt, 1); /* 2 */
    int b = proc_alloc(&pt, 1); /* 3 */

    /* Both children alive: waiting on either blocks. */
    ASSERT_EQ_INT(PROC_WAIT_BLOCK, proc_wait_find(&pt, 1, a, NULL));
    ASSERT_EQ_INT(PROC_WAIT_BLOCK, proc_wait_find(&pt, 1, -1, NULL));

    /* A exits with a status; exact-pid wait finds it, wait for B
     * still blocks. */
    ASSERT_EQ_INT(1, proc_exit(&pt, a, 42));
    int status = -1;
    ASSERT_EQ_INT(a, proc_wait_find(&pt, 1, a, &status));
    ASSERT_EQ_INT(42, status);
    ASSERT_EQ_INT(PROC_WAIT_BLOCK, proc_wait_find(&pt, 1, b, NULL));

    /* The zombie stays until reaped, then is gone. */
    ASSERT_EQ_INT(a, proc_wait_find(&pt, 1, -1, &status));
    proc_reap(&pt, a);
    ASSERT_EQ_INT(PROC_WAIT_BLOCK, proc_wait_find(&pt, 1, -1, NULL));
    ASSERT_TRUE(proc_find(&pt, a) == NULL);
}

TEST(proc_wait_nochild_cases) {
    struct proc_table pt;
    proc_table_init(&pt);
    proc_alloc(&pt, 0);         /* 1: init */
    int a = proc_alloc(&pt, 1); /* 2 */
    int c = proc_alloc(&pt, a); /* 3: grandchild */

    /* No children at all. */
    ASSERT_EQ_INT(PROC_WAIT_NOCHILD, proc_wait_find(&pt, c, -1, NULL));
    /* Existing process, but not this parent's child. */
    ASSERT_EQ_INT(PROC_WAIT_NOCHILD, proc_wait_find(&pt, 1, c, NULL));
    /* Nonexistent pid. */
    ASSERT_EQ_INT(PROC_WAIT_NOCHILD, proc_wait_find(&pt, 1, 99, NULL));
}

TEST(proc_orphans_reparent_to_init) {
    struct proc_table pt;
    proc_table_init(&pt);
    proc_alloc(&pt, 0);         /* 1: init */
    int a = proc_alloc(&pt, 1); /* 2 */
    int c = proc_alloc(&pt, a); /* 3: a's live child */
    int d = proc_alloc(&pt, a); /* 4: a's zombie child */
    ASSERT_EQ_INT(a, proc_exit(&pt, d, 7));

    /* a dies; both its live and zombie children become init's. */
    ASSERT_EQ_INT(1, proc_exit(&pt, a, 0));
    ASSERT_EQ_INT(1, proc_find(&pt, c)->ppid);
    ASSERT_EQ_INT(1, proc_find(&pt, d)->ppid);

    /* init can now reap the inherited zombie. */
    int status = -1;
    int got = proc_wait_find(&pt, 1, d, &status);
    ASSERT_EQ_INT(d, got);
    ASSERT_EQ_INT(7, status);
}

TEST(proc_state_violations_are_inert) {
    struct proc_table pt;
    proc_table_init(&pt);
    proc_alloc(&pt, 0);
    int a = proc_alloc(&pt, 1);

    /* Exiting a nonexistent or already-zombie pid does nothing. */
    ASSERT_EQ_INT(0, proc_exit(&pt, 99, 1));
    ASSERT_EQ_INT(1, proc_exit(&pt, a, 5));
    ASSERT_EQ_INT(0, proc_exit(&pt, a, 6));
    int status = -1;
    ASSERT_EQ_INT(a, proc_wait_find(&pt, 1, a, &status));
    ASSERT_EQ_INT(5, status); /* first exit wins */

    /* Reaping a live or absent pid does nothing. */
    proc_reap(&pt, 1);
    proc_reap(&pt, 99);
    ASSERT_EQ_INT(2, proc_count(&pt));
}
