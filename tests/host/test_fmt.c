/*
 * test_fmt.c - host unit tests for kernel/lib/fmt.c.
 *
 * Expected outputs are written out explicitly (rather than compared against
 * the host's snprintf) so the contract is pinned by the tests themselves and
 * cannot drift with libc differences.
 */
#include "test.h"

#include <stdint.h>

#include <fmt.h> /* kernel header; symbols renamed by build flags */

/* Format into a fresh buffer and check both the output and the return value. */
#define CHECK_FMT(expected, ...)                                                                   \
    do {                                                                                           \
        char buf_[256];                                                                            \
        int r_ = snprintf(buf_, sizeof(buf_), __VA_ARGS__);                                        \
        ASSERT_EQ_STR(expected, buf_);                                                             \
        ASSERT_EQ_INT((long long)strlen(expected), r_);                                            \
    } while (0)

TEST(fmt_plain_text) {
    CHECK_FMT("hello, world", "hello, world");
    CHECK_FMT("", "");
    CHECK_FMT("100%", "100%%");
}

TEST(fmt_decimal) {
    CHECK_FMT("0", "%d", 0);
    CHECK_FMT("42", "%d", 42);
    CHECK_FMT("-42", "%d", -42);
    CHECK_FMT("2147483647", "%d", INT32_MAX);
    CHECK_FMT("-2147483648", "%d", INT32_MIN);
    CHECK_FMT("9223372036854775807", "%lld", INT64_MAX);
    CHECK_FMT("-9223372036854775808", "%lld", INT64_MIN);
    CHECK_FMT("4294967295", "%u", UINT32_MAX);
    CHECK_FMT("18446744073709551615", "%llu", UINT64_MAX);
}

TEST(fmt_sign_flags) {
    CHECK_FMT("+42", "%+d", 42);
    CHECK_FMT("-42", "%+d", -42);
    CHECK_FMT(" 42", "% d", 42);
    CHECK_FMT("-42", "% d", -42);
    CHECK_FMT("+0", "%+d", 0);
}

TEST(fmt_width_and_zero_pad) {
    CHECK_FMT("   42", "%5d", 42);
    CHECK_FMT("42   ", "%-5d", 42);
    CHECK_FMT("00042", "%05d", 42);
    CHECK_FMT("-0042", "%05d", -42);
    CHECK_FMT("  -42", "%5d", -42);
    CHECK_FMT("42", "%1d", 42); /* width smaller than value */
    CHECK_FMT("+0042", "%+05d", 42);
}

TEST(fmt_precision_int) {
    CHECK_FMT("00042", "%.5d", 42);
    CHECK_FMT("  00042", "%7.5d", 42);
    CHECK_FMT("", "%.0d", 0); /* precision 0, value 0: no digits */
    CHECK_FMT("0", "%.1d", 0);
    /* '0' flag is ignored when precision is given. */
    CHECK_FMT("   00042", "%08.5d", 42);
}

TEST(fmt_hex_octal) {
    CHECK_FMT("2a", "%x", 42);
    CHECK_FMT("2A", "%X", 42);
    CHECK_FMT("0x2a", "%#x", 42);
    CHECK_FMT("0X2A", "%#X", 42);
    CHECK_FMT("0", "%#x", 0); /* no 0x prefix for zero */
    CHECK_FMT("ffffffffffffffff", "%llx", UINT64_MAX);
    CHECK_FMT("52", "%o", 42);
    CHECK_FMT("052", "%#o", 42);
    CHECK_FMT("0x00ff", "%#06x", 255);
}

TEST(fmt_length_modifiers) {
    CHECK_FMT("255", "%hhu", 0x1FF);    /* truncated to unsigned char */
    CHECK_FMT("-1", "%hhd", 0xFF);      /* truncated to signed char */
    CHECK_FMT("65535", "%hu", 0x1FFFF); /* truncated to unsigned short */
    CHECK_FMT("18446744073709551615", "%zu", (size_t)UINT64_MAX);
    CHECK_FMT("-5", "%td", (ptrdiff_t)-5);
    CHECK_FMT("34359738368", "%ld", 34359738368L);
}

TEST(fmt_char_and_string) {
    CHECK_FMT("x", "%c", 'x');
    CHECK_FMT("  x", "%3c", 'x');
    CHECK_FMT("x  ", "%-3c", 'x');
    CHECK_FMT("hello", "%s", "hello");
    CHECK_FMT("   hi", "%5s", "hi");
    CHECK_FMT("hi   ", "%-5s", "hi");
    CHECK_FMT("hel", "%.3s", "hello");
    CHECK_FMT("  hel", "%5.3s", "hello");
    CHECK_FMT("(null)", "%s", (const char *)NULL);
}

TEST(fmt_star_width_precision) {
    CHECK_FMT("   42", "%*d", 5, 42);
    CHECK_FMT("42   ", "%*d", -5, 42); /* negative width => left align */
    CHECK_FMT("hel", "%.*s", 3, "hello");
    CHECK_FMT("hello", "%.*s", -1, "hello"); /* negative precision => none */
    CHECK_FMT("  hel", "%*.*s", 5, 3, "hello");
}

TEST(fmt_pointer) {
    CHECK_FMT("0x0", "%p", (void *)0);
    CHECK_FMT("0x1000", "%p", (void *)0x1000);
    CHECK_FMT("0xffffffff80100000", "%p", (void *)0xffffffff80100000ULL);
}

/* This test feeds deliberately invalid conversions; silence the checker. */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat"
#pragma clang diagnostic ignored "-Wformat-invalid-specifier"
TEST(fmt_unknown_conversion_is_verbatim) {
    CHECK_FMT("%q", "%q");
    CHECK_FMT("a%qb", "a%qb");
}
#pragma clang diagnostic pop

TEST(fmt_truncation) {
    char buf[6];
    int r = snprintf(buf, sizeof(buf), "%s", "hello, world");
    ASSERT_EQ_INT(12, r); /* full length reported */
    ASSERT_EQ_STR("hello", buf);

    r = snprintf(buf, sizeof(buf), "%d", 1234567);
    ASSERT_EQ_INT(7, r);
    ASSERT_EQ_STR("12345", buf);
}

TEST(fmt_zero_size_buffer) {
    char sentinel = 'S';
    int r = snprintf(&sentinel, 0, "%d", 12345);
    ASSERT_EQ_INT(5, r);
    ASSERT_EQ_INT('S', sentinel); /* buffer untouched when size == 0 */
}

TEST(fmt_size_one_buffer) {
    char buf[1] = {'X'};
    int r = snprintf(buf, 1, "%s", "abc");
    ASSERT_EQ_INT(3, r);
    ASSERT_EQ_INT('\0', buf[0]); /* only the NUL fits */
}
