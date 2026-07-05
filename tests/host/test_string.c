/*
 * test_string.c - host unit tests for kernel/lib/string.c.
 *
 * The kernel lib is compiled for the host with its public symbols renamed
 * (memcpy -> hl_memcpy, ...) via -D flags in the Makefile so they cannot
 * collide with libc or sanitizer interceptors. This file is compiled with the
 * same -D flags, so the calls below resolve to the kernel implementations.
 */
#include "test.h"

#include <stdint.h>

#include <string.h> /* kernel header; symbols renamed by build flags */

TEST(memcpy_basic) {
    uint8_t src[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    uint8_t dst[8] = {0};
    void *r = memcpy(dst, src, sizeof(src));
    ASSERT_TRUE(r == dst);
    ASSERT_EQ_INT(0, memcmp(dst, src, sizeof(src)));
}

TEST(memcpy_zero_length) {
    uint8_t dst[1] = {42};
    memcpy(dst, (void *)0x1, 0); /* n==0 must not touch pointers */
    ASSERT_EQ_INT(42, dst[0]);
}

TEST(memmove_overlap_forward) {
    char buf[16] = "abcdefgh";
    memmove(buf + 2, buf, 6); /* dst > src */
    ASSERT_EQ_INT(0, memcmp(buf, "ababcdef", 8));
}

TEST(memmove_overlap_backward) {
    char buf[16] = "abcdefgh";
    memmove(buf, buf + 2, 6); /* dst < src */
    ASSERT_EQ_INT(0, memcmp(buf, "cdefghgh", 8));
}

TEST(memmove_same_pointer) {
    char buf[4] = "abc";
    memmove(buf, buf, 3);
    ASSERT_EQ_INT(0, memcmp(buf, "abc", 3));
}

TEST(memset_fill) {
    uint8_t buf[9];
    memset(buf, 0xAB, sizeof(buf));
    for (size_t i = 0; i < sizeof(buf); i++) {
        ASSERT_EQ_INT(0xAB, buf[i]);
    }
    /* Value is converted to unsigned char: 0x1FF -> 0xFF. */
    memset(buf, 0x1FF, 1);
    ASSERT_EQ_INT(0xFF, buf[0]);
}

TEST(memcmp_ordering) {
    ASSERT_EQ_INT(0, memcmp("abc", "abc", 3));
    ASSERT_TRUE(memcmp("abc", "abd", 3) < 0);
    ASSERT_TRUE(memcmp("abd", "abc", 3) > 0);
    /* Comparison is over unsigned bytes: 0x80 > 0x7F. */
    ASSERT_TRUE(memcmp("\x80", "\x7f", 1) > 0);
    ASSERT_EQ_INT(0, memcmp("x", "y", 0));
}

TEST(strlen_cases) {
    ASSERT_EQ_INT(0, strlen(""));
    ASSERT_EQ_INT(5, strlen("hello"));
}

TEST(strnlen_cases) {
    ASSERT_EQ_INT(3, strnlen("hello", 3));
    ASSERT_EQ_INT(5, strnlen("hello", 99));
    ASSERT_EQ_INT(0, strnlen("", 8));
}

TEST(strcmp_cases) {
    ASSERT_EQ_INT(0, strcmp("", ""));
    ASSERT_EQ_INT(0, strcmp("same", "same"));
    ASSERT_TRUE(strcmp("a", "b") < 0);
    ASSERT_TRUE(strcmp("b", "a") > 0);
    ASSERT_TRUE(strcmp("ab", "abc") < 0);    /* prefix orders first */
    ASSERT_TRUE(strcmp("\x80", "\x10") > 0); /* unsigned comparison */
}

TEST(strncmp_cases) {
    ASSERT_EQ_INT(0, strncmp("abcdef", "abcxyz", 3));
    ASSERT_TRUE(strncmp("abcdef", "abcxyz", 4) < 0);
    ASSERT_EQ_INT(0, strncmp("abc", "abc", 99)); /* stops at NUL */
    ASSERT_EQ_INT(0, strncmp("x", "y", 0));
}
