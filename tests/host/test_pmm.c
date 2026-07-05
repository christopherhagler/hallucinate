/*
 * test_pmm.c - host unit tests for the bitmap frame allocator core
 * (kernel/mm/pmm_core.c).
 */
#include "test.h"

#include <stdint.h>
#include <string.h>

#include <pmm_core.h>

#define NFRAMES 1024

static uint8_t bitmap[NFRAMES / 8];

static struct pmm fresh_all_free(void) {
    struct pmm p;
    pmm_core_init(&p, bitmap, NFRAMES);
    pmm_core_mark_free(&p, 0, NFRAMES);
    return p;
}

TEST(pmm_init_starts_fully_used) {
    struct pmm p;
    pmm_core_init(&p, bitmap, NFRAMES);
    ASSERT_EQ_INT(0, (int)p.free_frames);
    ASSERT_EQ_INT(-1, (int)pmm_core_alloc(&p));
    ASSERT_TRUE(!pmm_core_is_free(&p, 0));
}

TEST(pmm_mark_is_idempotent_and_exact) {
    struct pmm p;
    pmm_core_init(&p, bitmap, NFRAMES);
    pmm_core_mark_free(&p, 10, 20);
    ASSERT_EQ_INT(20, (int)p.free_frames);
    pmm_core_mark_free(&p, 15, 30); /* overlaps the first range */
    ASSERT_EQ_INT(35, (int)p.free_frames);
    pmm_core_mark_used(&p, 0, 100);
    ASSERT_EQ_INT(0, (int)p.free_frames);
    pmm_core_mark_used(&p, 0, 100); /* again: no double counting */
    ASSERT_EQ_INT(0, (int)p.free_frames);
    /* Clipping: ranges beyond nframes are ignored. */
    pmm_core_mark_free(&p, NFRAMES - 4, 100);
    ASSERT_EQ_INT(4, (int)p.free_frames);
}

TEST(pmm_alloc_free_roundtrip) {
    struct pmm p = fresh_all_free();
    int64_t a = pmm_core_alloc(&p);
    int64_t b = pmm_core_alloc(&p);
    ASSERT_TRUE(a >= 0 && b >= 0 && a != b);
    ASSERT_EQ_INT(NFRAMES - 2, (int)p.free_frames);
    ASSERT_EQ_INT(0, pmm_core_free(&p, (uint64_t)a));
    ASSERT_EQ_INT(NFRAMES - 1, (int)p.free_frames);
    /* Double free is rejected and changes nothing. */
    ASSERT_EQ_INT(-1, pmm_core_free(&p, (uint64_t)a));
    ASSERT_EQ_INT(NFRAMES - 1, (int)p.free_frames);
    /* Out-of-range free is rejected. */
    ASSERT_EQ_INT(-1, pmm_core_free(&p, NFRAMES));
}

TEST(pmm_exhaustion_and_recovery) {
    struct pmm p = fresh_all_free();
    for (int i = 0; i < NFRAMES; i++) {
        ASSERT_TRUE(pmm_core_alloc(&p) >= 0);
    }
    ASSERT_EQ_INT(0, (int)p.free_frames);
    ASSERT_EQ_INT(-1, (int)pmm_core_alloc(&p));
    ASSERT_EQ_INT(0, pmm_core_free(&p, 123));
    int64_t again = pmm_core_alloc(&p);
    ASSERT_EQ_INT(123, (int)again);
}

TEST(pmm_alloc_no_double_handout) {
    struct pmm p = fresh_all_free();
    /* Every allocated frame must be unique: mark seen frames. */
    static uint8_t seen[NFRAMES];
    memset(seen, 0, sizeof(seen));
    for (int i = 0; i < NFRAMES; i++) {
        int64_t f = pmm_core_alloc(&p);
        ASSERT_TRUE(f >= 0 && f < NFRAMES);
        ASSERT_TRUE(!seen[f]);
        seen[f] = 1;
    }
}

TEST(pmm_alloc_run_contiguous_aligned) {
    struct pmm p = fresh_all_free();
    /* Poke holes so the allocator has to search. */
    pmm_core_mark_used(&p, 0, 3);
    pmm_core_mark_used(&p, 8, 1);
    int64_t run = pmm_core_alloc_run(&p, 8, 8);
    ASSERT_EQ_INT(16, (int)run); /* 0-7 blocked by 0-2, 8-15 by 8 */
    for (int i = 0; i < 8; i++) {
        ASSERT_TRUE(!pmm_core_is_free(&p, (uint64_t)(run + i)));
    }
    /* Unaligned request still fits the first gap. */
    int64_t small = pmm_core_alloc_run(&p, 5, 1);
    ASSERT_EQ_INT(3, (int)small);
}

TEST(pmm_alloc_run_rejects_bad_args) {
    struct pmm p = fresh_all_free();
    ASSERT_EQ_INT(-1, (int)pmm_core_alloc_run(&p, 0, 1));
    ASSERT_EQ_INT(-1, (int)pmm_core_alloc_run(&p, 1, 0));
    ASSERT_EQ_INT(-1, (int)pmm_core_alloc_run(&p, 1, 3)); /* not a power of 2 */
    ASSERT_EQ_INT(-1, (int)pmm_core_alloc_run(&p, NFRAMES + 1, 1));
}
