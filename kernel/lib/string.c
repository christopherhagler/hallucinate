/*
 * string.c - freestanding memory and string primitives.
 *
 * Correctness over cleverness: byte-wise loops, no undefined behavior, no
 * alignment assumptions. Word-sized fast paths can be added later behind the
 * same interface once profiling justifies them.
 */
#include <string.h>

#include <stdint.h>

void *memcpy(void *dst, const void *src, size_t n) {
    uint8_t *d = dst;
    const uint8_t *s = src;
    for (size_t i = 0; i < n; i++) {
        d[i] = s[i];
    }
    return dst;
}

void *memmove(void *dst, const void *src, size_t n) {
    uint8_t *d = dst;
    const uint8_t *s = src;
    if (d == s || n == 0) {
        return dst;
    }
    /* Compare as integers to avoid UB from relational pointer comparison
     * between objects; the result only needs to pick a safe copy direction. */
    if ((uintptr_t)d < (uintptr_t)s) {
        for (size_t i = 0; i < n; i++) {
            d[i] = s[i];
        }
    } else {
        for (size_t i = n; i > 0; i--) {
            d[i - 1] = s[i - 1];
        }
    }
    return dst;
}

void *memset(void *dst, int c, size_t n) {
    uint8_t *d = dst;
    for (size_t i = 0; i < n; i++) {
        d[i] = (uint8_t)c;
    }
    return dst;
}

int memcmp(const void *a, const void *b, size_t n) {
    const uint8_t *pa = a;
    const uint8_t *pb = b;
    for (size_t i = 0; i < n; i++) {
        if (pa[i] != pb[i]) {
            return pa[i] < pb[i] ? -1 : 1;
        }
    }
    return 0;
}

size_t strlen(const char *s) {
    size_t n = 0;
    while (s[n] != '\0') {
        n++;
    }
    return n;
}

size_t strnlen(const char *s, size_t max) {
    size_t n = 0;
    while (n < max && s[n] != '\0') {
        n++;
    }
    return n;
}

int strcmp(const char *a, const char *b) {
    while (*a != '\0' && *a == *b) {
        a++;
        b++;
    }
    if ((uint8_t)*a < (uint8_t)*b) {
        return -1;
    }
    return (uint8_t)*a > (uint8_t)*b ? 1 : 0;
}

int strncmp(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i]) {
            return (uint8_t)a[i] < (uint8_t)b[i] ? -1 : 1;
        }
        if (a[i] == '\0') {
            return 0;
        }
    }
    return 0;
}
