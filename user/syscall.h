/*
 * syscall.h - userspace system call interface (native ABI).
 *
 * The native ABI is the Linux x86_64 convention: number in rax,
 * arguments in rdi/rsi/rdx/r10/r8/r9, result in rax, errors returned
 * as -errno. The `syscall` instruction clobbers rcx and r11
 * (hardware behavior). See docs/userspace.md.
 */
#pragma once

#include <stdint.h>

#define SYS_write  1
#define SYS_getpid 39
#define SYS_fork   57
#define SYS_execve 59
#define SYS_exit   60
#define SYS_wait4  61

/* Error numbers (Linux x86_64 values, negated in return values). */
#define EBADF  9
#define ECHILD 10
#define EFAULT 14
#define ENOSYS 38

static inline long syscall0(long nr) {
    long ret;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(nr) : "rcx", "r11", "memory");
    return ret;
}

static inline long syscall1(long nr, long a1) {
    long ret;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(nr), "D"(a1) : "rcx", "r11", "memory");
    return ret;
}

static inline long syscall3(long nr, long a1, long a2, long a3) {
    long ret;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(nr), "D"(a1), "S"(a2), "d"(a3)
                     : "rcx", "r11", "memory");
    return ret;
}

static inline long syscall4(long nr, long a1, long a2, long a3, long a4) {
    register long r10 __asm__("r10") = a4;
    long ret;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(nr), "D"(a1), "S"(a2), "d"(a3), "r"(r10)
                     : "rcx", "r11", "memory");
    return ret;
}

static inline long sys_write(int fd, const void *buf, unsigned long count) {
    return syscall3(SYS_write, fd, (long)(uintptr_t)buf, (long)count);
}

static inline long sys_getpid(void) {
    return syscall0(SYS_getpid);
}

static inline long sys_fork(void) {
    return syscall0(SYS_fork);
}

static inline long sys_execve(const char *path, const char *const argv[],
                              const char *const envp[]) {
    return syscall3(SYS_execve, (long)(uintptr_t)path, (long)(uintptr_t)argv,
                    (long)(uintptr_t)envp);
}

static inline long sys_wait4(long pid, int *wstatus, long options) {
    return syscall4(SYS_wait4, pid, (long)(uintptr_t)wstatus, options, 0);
}

__attribute__((noreturn)) static inline void sys_exit(int status) {
    syscall1(SYS_exit, status);
    __builtin_unreachable();
}
