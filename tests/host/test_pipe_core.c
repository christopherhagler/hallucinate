/*
 * test_pipe_core.c - host tests for the pipe ring buffer (pipe_core.h).
 */
#include <pipe_core.h>

#include "test.h"

TEST(pipe_core_starts_empty) {
    uint8_t buf[8];
    struct pipe_core pc;
    pipe_core_init(&pc, buf, sizeof(buf));
    ASSERT_EQ_INT(0, pipe_core_avail(&pc));
    ASSERT_EQ_INT(sizeof(buf), pipe_core_space(&pc));
    uint8_t dst[1];
    ASSERT_EQ_INT(0, pipe_core_read(&pc, dst, sizeof(dst)));
}

TEST(pipe_core_simple_roundtrip) {
    uint8_t buf[8];
    struct pipe_core pc;
    pipe_core_init(&pc, buf, sizeof(buf));
    ASSERT_EQ_INT(5, pipe_core_write(&pc, (const uint8_t *)"hello", 5));
    ASSERT_EQ_INT(5, pipe_core_avail(&pc));
    ASSERT_EQ_INT(3, pipe_core_space(&pc));
    uint8_t out[8] = {0};
    ASSERT_EQ_INT(5, pipe_core_read(&pc, out, sizeof(out)));
    ASSERT_TRUE(memcmp(out, "hello", 5) == 0);
    ASSERT_EQ_INT(0, pipe_core_avail(&pc));
}

TEST(pipe_core_write_reports_short_when_full) {
    uint8_t buf[4];
    struct pipe_core pc;
    pipe_core_init(&pc, buf, sizeof(buf));
    ASSERT_EQ_INT(4, pipe_core_write(&pc, (const uint8_t *)"abcdef", 6));
    ASSERT_EQ_INT(0, pipe_core_space(&pc));
    ASSERT_EQ_INT(0, pipe_core_write(&pc, (const uint8_t *)"z", 1));
}

TEST(pipe_core_read_reports_short_when_underfull) {
    uint8_t buf[8];
    struct pipe_core pc;
    pipe_core_init(&pc, buf, sizeof(buf));
    pipe_core_write(&pc, (const uint8_t *)"ab", 2);
    uint8_t out[8] = {0};
    ASSERT_EQ_INT(2, pipe_core_read(&pc, out, sizeof(out)));
    ASSERT_TRUE(memcmp(out, "ab", 2) == 0);
}

TEST(pipe_core_wraps_around) {
    uint8_t buf[4];
    struct pipe_core pc;
    pipe_core_init(&pc, buf, sizeof(buf));
    /* Fill, drain most, refill past the wrap point, and check the
     * bytes come back in write order regardless of the wraparound. */
    pipe_core_write(&pc, (const uint8_t *)"AB", 2);
    uint8_t out[4] = {0};
    pipe_core_read(&pc, out, 1); /* drain "A"; head now at 1 */
    ASSERT_EQ_INT(3, pipe_core_write(&pc, (const uint8_t *)"CDE", 3));
    /* buffer now holds "BCDE" logically, head=1 count=4, physically
     * wrapped across the 4-byte array. */
    ASSERT_EQ_INT(4, pipe_core_read(&pc, out, sizeof(out)));
    ASSERT_TRUE(memcmp(out, "BCDE", 4) == 0);
}

TEST(pipe_core_interleaved_partial_transfers) {
    uint8_t buf[3];
    struct pipe_core pc;
    pipe_core_init(&pc, buf, sizeof(buf));
    for (int round = 0; round < 100; round++) {
        uint8_t w = (uint8_t)round;
        ASSERT_EQ_INT(1, pipe_core_write(&pc, &w, 1));
        uint8_t r = 0xff;
        ASSERT_EQ_INT(1, pipe_core_read(&pc, &r, 1));
        ASSERT_EQ_INT(w, r);
    }
    ASSERT_EQ_INT(0, pipe_core_avail(&pc));
}

TEST(pipe_core_randomized_stress_vs_shadow) {
    enum { CAP = 37 };
    static uint8_t buf[CAP];
    struct pipe_core pc;
    pipe_core_init(&pc, buf, CAP);

    /* Shadow: an infinite-capacity FIFO byte queue modeled as an
     * array with head/count, generous enough it never wraps. */
    enum { SHADOW_CAP = 1u << 16 };
    static uint8_t shadow[SHADOW_CAP];
    size_t shead = 0;
    size_t scount = 0;

    unsigned seed = 987654321u;
    for (int round = 0; round < 50000; round++) {
        seed = (seed * 1103515245u) + 12345u;
        int do_write = (int)((seed >> 16) & 1);
        seed = (seed * 1103515245u) + 12345u;
        size_t len = 1 + ((seed >> 8) % 10);

        if (do_write) {
            static uint8_t src[16];
            for (size_t i = 0; i < len; i++) {
                seed = (seed * 1103515245u) + 12345u;
                src[i] = (uint8_t)(seed >> 24);
            }
            size_t n = pipe_core_write(&pc, src, len);
            ASSERT_TRUE(n <= len);
            for (size_t i = 0; i < n; i++) {
                ASSERT_TRUE(scount < SHADOW_CAP);
                shadow[(shead + scount) % SHADOW_CAP] = src[i];
                scount++;
            }
        } else {
            static uint8_t dst[16];
            size_t n = pipe_core_read(&pc, dst, len);
            ASSERT_TRUE(n <= len);
            ASSERT_EQ_INT((long long)(scount < len ? scount : len), (long long)n);
            for (size_t i = 0; i < n; i++) {
                ASSERT_EQ_INT(shadow[(shead + i) % SHADOW_CAP], dst[i]);
            }
            shead = (shead + n) % SHADOW_CAP;
            scount -= n;
        }
        ASSERT_EQ_INT((long long)scount, (long long)pipe_core_avail(&pc));
        ASSERT_EQ_INT((long long)(CAP - scount), (long long)pipe_core_space(&pc));
    }
}
