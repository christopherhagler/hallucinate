/*
 * syscall.c - system call dispatch and implementations.
 *
 * ABI in syscall.h. Every user pointer is validated against the
 * calling thread's address space (present + user-accessible on every
 * page) before the kernel touches it; the user address space is
 * active during a syscall, so validated pointers are then plain
 * dereferences.
 */
#include <syscall.h>

#include <stddef.h>

#include <arch/x86_64/paging.h>
#include <console.h>
#include <errno.h>
#include <memlayout.h>
#include <pmm.h>
#include <process.h>
#include <sched.h>

#define ERR(e) ((uint64_t)-(e))

static int user_range_ok(uint64_t start, uint64_t len) {
    if (start >= USER_VA_LIMIT || len > USER_VA_LIMIT - start) {
        return 0;
    }
    struct addrspace *as = thread_current()->as;
    if (as == NULL) {
        return 0;
    }
    uint64_t first = start & ~(uint64_t)(PAGE_SIZE - 1);
    for (uint64_t va = first; va < start + len; va += PAGE_SIZE) {
        uint64_t pte = paging_lookup(as, va, NULL);
        if ((pte & PTE_P) == 0 || (pte & PTE_US) == 0) {
            return 0;
        }
    }
    return 1;
}

/* write(fd, buf, count): fd 1 (stdout) and 2 (stderr) both reach the
 * kernel console until file descriptors exist (Phase 5). */
static uint64_t sys_write(uint64_t fd, uint64_t buf, uint64_t count) {
    if (fd != 1 && fd != 2) {
        return ERR(EBADF);
    }
    if (count == 0) {
        return 0;
    }
    if (!user_range_ok(buf, count)) {
        return ERR(EFAULT);
    }
    console_write((const char *)buf, count);
    return count;
}

static uint64_t sys_getpid(void) {
    return INIT_PID;
}

uint64_t syscall_dispatch(uint64_t nr, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4) {
    (void)a4;
    switch (nr) {
    case SYS_write:
        return sys_write(a1, a2, a3);
    case SYS_getpid:
        return sys_getpid();
    case SYS_exit:
        process_exit((int)a1);
    default:
        return ERR(ENOSYS);
    }
}
