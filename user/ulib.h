/*
 * ulib.h - the few string helpers user programs need until a real
 * userspace libc exists. Freestanding, header-only.
 */
#pragma once

static inline unsigned long str_len(const char *s) {
    unsigned long n = 0;
    while (s[n] != '\0') {
        n++;
    }
    return n;
}

static inline int str_eq(const char *a, const char *b) {
    unsigned long i = 0;
    while (a[i] != '\0' && a[i] == b[i]) {
        i++;
    }
    return a[i] == b[i];
}
