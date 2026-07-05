/*
 * selftest.c - in-kernel boot-time self-tests.
 *
 * A thin sanity layer proving the freestanding lib behaves in the real kernel
 * environment (higher half, no SSE, -mcmodel=kernel). The exhaustive suite
 * for the same code runs on the host under sanitizers (tests/host/).
 */
#include <selftest.h>

#include <stddef.h>
#include <stdint.h>

#include <arch/x86_64/trap.h>
#include <fmt.h>
#include <kprintf.h>
#include <memlayout.h>
#include <panic.h>
#include <pmm.h>
#include <string.h>

static int assertions;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        assertions++;                                                                              \
        if (!(cond)) {                                                                             \
            panic("selftest: %s", #cond);                                                          \
        }                                                                                          \
    } while (0)

static void check_fmt(const char *expected, const char *got) {
    assertions++;
    if (strcmp(expected, got) != 0) {
        panic("selftest: fmt: expected \"%s\", got \"%s\"", expected, got);
    }
}

static void test_string(void) {
    uint8_t a[16];
    uint8_t b[16];
    memset(a, 0x5A, sizeof(a));
    memcpy(b, a, sizeof(a));
    CHECK(memcmp(a, b, sizeof(a)) == 0);

    char buf[9];
    memcpy(buf, "abcdefgh", 9);
    memmove(buf + 2, buf, 6);
    CHECK(memcmp(buf, "ababcdef", 8) == 0);

    CHECK(strlen("hallucinate") == 11);
    CHECK(strnlen("hallucinate", 5) == 5);
    CHECK(strcmp("boot", "boot") == 0);
    CHECK(strncmp("kernel", "kernfs", 4) == 0);
}

static void test_fmt(void) {
    char buf[64];

    snprintf(buf, sizeof(buf), "%d %u %x", -42, 42u, 0xBEEFu);
    check_fmt("-42 42 beef", buf);

    snprintf(buf, sizeof(buf), "%#018llx", 0xffffffff80100000ull);
    check_fmt("0xffffffff80100000", buf); /* exactly 18 wide incl. 0x */

    snprintf(buf, sizeof(buf), "%5s|%-5s|%.2s", "ab", "cd", "efgh");
    check_fmt("   ab|cd   |ef", buf);

    snprintf(buf, sizeof(buf), "%p", (void *)0x6000);
    check_fmt("0x6000", buf);

    int want = snprintf(buf, 4, "%s", "truncated");
    CHECK(want == 9);
    check_fmt("tru", buf);
}

static int breakpoints_taken;

static void breakpoint_handler(struct trapframe *tf) {
    /* #BP is a trap: RIP already points past the int3. */
    breakpoints_taken++;
    assertions++;
    if (tf->vector != VEC_BREAKPOINT) {
        panic("selftest: breakpoint handler got vector %llu", (unsigned long long)tf->vector);
    }
}

static void test_traps(void) {
    breakpoints_taken = 0;
    trap_handler_t prev = trap_register(VEC_BREAKPOINT, breakpoint_handler);
    CHECK(prev == NULL);
    __asm__ volatile("int3");
    CHECK(breakpoints_taken == 1);
    trap_register(VEC_BREAKPOINT, prev);
}

static void test_pmm(void) {
    uint64_t free_before = pmm_free_frames();
    CHECK(free_before > 0);

    /* Allocate a batch of frames: page-aligned, distinct, writable. */
    enum { BATCH = 32 };
    uint64_t frames[BATCH];
    for (int i = 0; i < BATCH; i++) {
        frames[i] = pmm_alloc_frame();
        CHECK(frames[i] != 0);
        CHECK((frames[i] & (PAGE_SIZE - 1)) == 0);
        CHECK(frames[i] < BOOT_MAPPED_LIMIT);
        for (int j = 0; j < i; j++) {
            CHECK(frames[i] != frames[j]);
        }
        /* Prove the frame is real, mapped memory. */
        uint8_t *mem = phys_to_virt(frames[i]);
        memset(mem, 0xA5 + i, PAGE_SIZE);
        CHECK(mem[0] == (uint8_t)(0xA5 + i) && mem[PAGE_SIZE - 1] == (uint8_t)(0xA5 + i));
    }
    CHECK(pmm_free_frames() == free_before - BATCH);

    /* Contiguous run, 8 frames aligned to 8. */
    uint64_t run = pmm_alloc_frames(8, 8);
    CHECK(run != 0);
    CHECK((run & ((8 * PAGE_SIZE) - 1)) == 0);

    for (int i = 0; i < BATCH; i++) {
        pmm_free_frame(frames[i]);
    }
    for (int i = 0; i < 8; i++) {
        pmm_free_frame(run + ((uint64_t)i * PAGE_SIZE));
    }
    CHECK(pmm_free_frames() == free_before);
}

void selftest_run(void) {
    assertions = 0;
    test_string();
    test_fmt();
    test_traps();
    test_pmm();
    kprintf("selftest: passed (%d assertions)\n", assertions);
}
