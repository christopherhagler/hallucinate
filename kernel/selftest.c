/*
 * selftest.c - in-kernel boot-time self-tests.
 *
 * A thin sanity layer proving the freestanding lib behaves in the real kernel
 * environment (higher half, no SSE, -mcmodel=kernel). The exhaustive suite
 * for the same code runs on the host under sanitizers (tests/host/).
 */
#include <selftest.h>

#include <stdint.h>

#include <fmt.h>
#include <kprintf.h>
#include <panic.h>
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

void selftest_run(void) {
    assertions = 0;
    test_string();
    test_fmt();
    kprintf("selftest: passed (%d assertions)\n", assertions);
}
