/*
 * uaccess.h - validated access to user memory.
 *
 * Every user pointer entering the kernel goes through these. The
 * calling thread's user address space is active during a syscall, so
 * a validated range is then a plain dereference — validation checks
 * that every touched page is present and user-accessible (and
 * writable, for writes) in the caller's address space, and that the
 * range lies below USER_VA_LIMIT. Anything else is -EFAULT without
 * the kernel touching a byte.
 */
#pragma once

#include <stdint.h>

/* Nonzero when [start, start+len) is fully user-accessible; `write`
 * additionally requires every page to be writable. len 0 is ok. */
int user_range_ok(uint64_t start, uint64_t len, int write);

/* Copy in/out. 0 on success, -EFAULT on a bad range. */
long user_copy_from(void *dst, uint64_t uaddr, uint64_t len);
long user_copy_to(uint64_t uaddr, const void *src, uint64_t len);

/*
 * Copy a NUL-terminated user string into dst (capacity `cap`,
 * including the NUL). Returns the string length, -EFAULT on a bad
 * range, or -ENAMETOOLONG if no NUL appears within cap bytes.
 * Validates page by page, so the string may end on the last mapped
 * page without faulting on the next.
 */
long user_strncpy(char *dst, uint64_t uaddr, uint64_t cap);
