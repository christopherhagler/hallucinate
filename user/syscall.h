/*
 * syscall.h - userspace system call interface (native ABI).
 *
 * The native ABI is the Linux x86_64 convention: number in rax,
 * arguments in rdi/rsi/rdx/r10/r8/r9, result in rax, errors returned
 * as -errno. The `syscall` instruction clobbers rcx and r11
 * (hardware behavior). See docs/book/appendix-h-userspace.md.
 */
#pragma once

#include <stdint.h>

#define SYS_read       0
#define SYS_write      1
#define SYS_open       2
#define SYS_close      3
#define SYS_fstat      5
#define SYS_lseek      8
#define SYS_getpid     39
#define SYS_fork       57
#define SYS_execve     59
#define SYS_exit       60
#define SYS_wait4      61
#define SYS_fsync      74
#define SYS_rename     82
#define SYS_mkdir      83
#define SYS_rmdir      84
#define SYS_link       86
#define SYS_unlink     87
#define SYS_getdents64 217

/* Error numbers (Linux x86_64 values, negated in return values). */
#define EPERM     1
#define ENOENT    2
#define EBADF     9
#define ECHILD    10
#define EFAULT    14
#define EBUSY     16
#define EEXIST    17
#define EXDEV     18
#define ENOTDIR   20
#define EISDIR    21
#define EINVAL    22
#define ESPIPE    29
#define EROFS     30
#define ENOSYS    38
#define ENOTEMPTY 39

/* open(2) flags (Linux x86_64 values). */
#define O_RDONLY    0
#define O_WRONLY    1
#define O_RDWR      2
#define O_CREAT     0100
#define O_EXCL      0200
#define O_TRUNC     01000
#define O_APPEND    02000
#define O_DIRECTORY 0200000

/* lseek(2) whence. */
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

/* st_mode file-type field. */
#define S_IFMT  0170000u
#define S_IFREG 0100000u
#define S_IFDIR 0040000u
#define S_IFCHR 0020000u

/* getdents64 d_type values. */
#define DT_CHR 2u
#define DT_DIR 4u
#define DT_REG 8u

/* Linux x86_64 struct stat (matches the kernel's stat.h). */
struct stat {
    uint64_t st_dev;
    uint64_t st_ino;
    uint64_t st_nlink;
    uint32_t st_mode;
    uint32_t st_uid;
    uint32_t st_gid;
    uint32_t pad0_;
    uint64_t st_rdev;
    int64_t st_size;
    int64_t st_blksize;
    int64_t st_blocks;
    int64_t st_atime_sec, st_atime_nsec;
    int64_t st_mtime_sec, st_mtime_nsec;
    int64_t st_ctime_sec, st_ctime_nsec;
    int64_t unused_[3];
};

/* getdents64 record: d_reclen bytes, name NUL-terminated. */
struct dirent64 {
    uint64_t d_ino;
    int64_t d_off;
    uint16_t d_reclen;
    uint8_t d_type;
    char d_name[];
};

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

static inline long sys_read(int fd, void *buf, unsigned long count) {
    return syscall3(SYS_read, fd, (long)(uintptr_t)buf, (long)count);
}

static inline long sys_write(int fd, const void *buf, unsigned long count) {
    return syscall3(SYS_write, fd, (long)(uintptr_t)buf, (long)count);
}

static inline long sys_open(const char *path, long flags) {
    return syscall3(SYS_open, (long)(uintptr_t)path, flags, 0);
}

static inline long sys_open3(const char *path, long flags, long mode) {
    return syscall3(SYS_open, (long)(uintptr_t)path, flags, mode);
}

static inline long sys_fsync(int fd) {
    return syscall1(SYS_fsync, fd);
}

static inline long sys_mkdir(const char *path, long mode) {
    return syscall3(SYS_mkdir, (long)(uintptr_t)path, mode, 0);
}

static inline long sys_rmdir(const char *path) {
    return syscall1(SYS_rmdir, (long)(uintptr_t)path);
}

static inline long sys_unlink(const char *path) {
    return syscall1(SYS_unlink, (long)(uintptr_t)path);
}

static inline long sys_link(const char *oldpath, const char *newpath) {
    return syscall3(SYS_link, (long)(uintptr_t)oldpath, (long)(uintptr_t)newpath, 0);
}

static inline long sys_rename(const char *oldpath, const char *newpath) {
    return syscall3(SYS_rename, (long)(uintptr_t)oldpath, (long)(uintptr_t)newpath, 0);
}

static inline long sys_close(int fd) {
    return syscall1(SYS_close, fd);
}

static inline long sys_fstat(int fd, struct stat *st) {
    return syscall3(SYS_fstat, fd, (long)(uintptr_t)st, 0);
}

static inline long sys_lseek(int fd, long off, int whence) {
    return syscall3(SYS_lseek, fd, off, whence);
}

static inline long sys_getdents64(int fd, void *dirp, unsigned long count) {
    return syscall3(SYS_getdents64, fd, (long)(uintptr_t)dirp, (long)count);
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
