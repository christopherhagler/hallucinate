/*
 * uaccess.c - validated access to user memory (see uaccess.h).
 */
#include <uaccess.h>

#include <stddef.h>

#include <arch/x86_64/paging.h>
#include <errno.h>
#include <memlayout.h>
#include <pmm.h>
#include <sched.h>
#include <string.h>

int user_range_ok(uint64_t start, uint64_t len, int write) {
    if (start >= USER_VA_LIMIT || len > USER_VA_LIMIT - start) {
        return 0;
    }
    struct addrspace *as = thread_current()->as;
    if (as == NULL) {
        return 0;
    }
    uint64_t need = PTE_P | PTE_US | (write ? PTE_W : 0);
    uint64_t first = start & ~(uint64_t)(PAGE_SIZE - 1);
    for (uint64_t va = first; va < start + len; va += PAGE_SIZE) {
        uint64_t pte = paging_lookup(as, va, NULL);
        if ((pte & need) != need) {
            return 0;
        }
    }
    return 1;
}

long user_copy_from(void *dst, uint64_t uaddr, uint64_t len) {
    if (!user_range_ok(uaddr, len, 0)) {
        return -EFAULT;
    }
    memcpy(dst, (const void *)uaddr, len);
    return 0;
}

long user_copy_to(uint64_t uaddr, const void *src, uint64_t len) {
    if (!user_range_ok(uaddr, len, 1)) {
        return -EFAULT;
    }
    memcpy((void *)uaddr, src, len);
    return 0;
}

long user_strncpy(char *dst, uint64_t uaddr, uint64_t cap) {
    uint64_t done = 0;
    while (done < cap) {
        /* Validate only up to the end of the current user page: the
         * string may legitimately end on the last mapped one. */
        uint64_t page_left = PAGE_SIZE - ((uaddr + done) & (PAGE_SIZE - 1));
        uint64_t chunk = page_left < cap - done ? page_left : cap - done;
        if (!user_range_ok(uaddr + done, chunk, 0)) {
            return -EFAULT;
        }
        const char *src = (const char *)(uaddr + done);
        for (uint64_t i = 0; i < chunk; i++) {
            dst[done + i] = src[i];
            if (src[i] == '\0') {
                return (long)(done + i);
            }
        }
        done += chunk;
    }
    return -ENAMETOOLONG;
}
