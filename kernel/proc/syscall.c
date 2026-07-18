/*
 * syscall.c - system call dispatch and the file syscalls.
 *
 * ABI in syscall.h. This layer owns user-pointer validation (a
 * validated range is a plain pointer below, because the caller's
 * address space is active for the whole syscall) and the fd-number →
 * struct file step; the semantics live in the VFS file ops. The
 * process-model syscalls (fork/execve/wait4/exit) live in process.c
 * and get the syscall frame where they need the full user context.
 *
 * A NULL file-op slot maps to the conventional errno per operation:
 * read -EINVAL, write -EBADF, lseek -ESPIPE, getdents64 -ENOTDIR,
 * fsync -EINVAL. Access-mode enforcement (may *this description*
 * read or write?) lives inside the ops, not here.
 */
#include <syscall.h>

#include <stddef.h>

#include <errno.h>
#include <process.h>
#include <stat.h>
#include <uaccess.h>
#include <vfs.h>

#define ERR(e) ((uint64_t)-(e))

/* The asm entry stub and struct syscall_frame must agree exactly. */
_Static_assert(sizeof(struct syscall_frame) == 16ull * 8, "frame is 16 pushes");
_Static_assert(offsetof(struct syscall_frame, r15) == 0ull * 8, "r15 pushed last");
_Static_assert(offsetof(struct syscall_frame, rbx) == 5ull * 8, "layout");
_Static_assert(offsetof(struct syscall_frame, rdi) == 11ull * 8, "layout");
_Static_assert(offsetof(struct syscall_frame, rax) == 12ull * 8, "layout");
_Static_assert(offsetof(struct syscall_frame, rflags) == 13ull * 8, "layout");
_Static_assert(offsetof(struct syscall_frame, rip) == 14ull * 8, "layout");
_Static_assert(offsetof(struct syscall_frame, rsp) == 15ull * 8, "rsp pushed first");

static uint64_t sys_read(uint64_t fd, uint64_t buf, uint64_t count) {
    struct file *f = process_file_get((int)fd);
    if (f == NULL) {
        return ERR(EBADF);
    }
    if (f->ops->read == NULL) {
        return ERR(EINVAL);
    }
    if (count == 0) {
        return 0;
    }
    if (!user_range_ok(buf, count, 1)) {
        return ERR(EFAULT);
    }
    return (uint64_t)f->ops->read(f, (void *)buf, count);
}

static uint64_t sys_write(uint64_t fd, uint64_t buf, uint64_t count) {
    struct file *f = process_file_get((int)fd);
    if (f == NULL || f->ops->write == NULL) {
        return ERR(EBADF);
    }
    if (count == 0) {
        return 0;
    }
    if (!user_range_ok(buf, count, 0)) {
        return ERR(EFAULT);
    }
    return (uint64_t)f->ops->write(f, (const void *)buf, count);
}

static uint64_t sys_open(uint64_t upath, uint64_t flags, uint64_t mode) {
    char path[VFS_PATH_MAX];
    long rc = user_strncpy(path, upath, sizeof(path));
    if (rc < 0) {
        return (uint64_t)rc;
    }
    struct file *f = NULL;
    rc = vfs_open(path, (int)flags, (unsigned)mode, &f);
    if (rc != 0) {
        return (uint64_t)rc;
    }
    rc = process_fd_install(f);
    if (rc < 0) {
        vfs_file_put(f);
    }
    return (uint64_t)rc;
}

static uint64_t sys_close(uint64_t fd) {
    return (uint64_t)process_fd_close((int)fd);
}

static uint64_t sys_fstat(uint64_t fd, uint64_t ustat) {
    struct file *f = process_file_get((int)fd);
    if (f == NULL) {
        return ERR(EBADF);
    }
    struct stat st;
    long rc = f->ops->fstat(f, &st);
    if (rc != 0) {
        return (uint64_t)rc;
    }
    if (user_copy_to(ustat, &st, sizeof(st)) != 0) {
        return ERR(EFAULT);
    }
    return 0;
}

static uint64_t sys_lseek(uint64_t fd, uint64_t off, uint64_t whence) {
    struct file *f = process_file_get((int)fd);
    if (f == NULL) {
        return ERR(EBADF);
    }
    if (f->ops->lseek == NULL) {
        return ERR(ESPIPE);
    }
    return (uint64_t)f->ops->lseek(f, (int64_t)off, (int)whence);
}

static uint64_t sys_fsync(uint64_t fd) {
    struct file *f = process_file_get((int)fd);
    if (f == NULL) {
        return ERR(EBADF);
    }
    if (f->ops->fsync == NULL) {
        return ERR(EINVAL); /* special file with nothing to sync */
    }
    return (uint64_t)f->ops->fsync(f);
}

static uint64_t sys_mkdir(uint64_t upath, uint64_t mode) {
    char path[VFS_PATH_MAX];
    long rc = user_strncpy(path, upath, sizeof(path));
    if (rc < 0) {
        return (uint64_t)rc;
    }
    return (uint64_t)vfs_mkdir(path, (unsigned)mode);
}

static uint64_t sys_rmdir(uint64_t upath) {
    char path[VFS_PATH_MAX];
    long rc = user_strncpy(path, upath, sizeof(path));
    if (rc < 0) {
        return (uint64_t)rc;
    }
    return (uint64_t)vfs_rmdir(path);
}

static uint64_t sys_unlink(uint64_t upath) {
    char path[VFS_PATH_MAX];
    long rc = user_strncpy(path, upath, sizeof(path));
    if (rc < 0) {
        return (uint64_t)rc;
    }
    return (uint64_t)vfs_unlink(path);
}

static uint64_t sys_link(uint64_t uold, uint64_t unew) {
    char oldp[VFS_PATH_MAX];
    char newp[VFS_PATH_MAX];
    long rc = user_strncpy(oldp, uold, sizeof(oldp));
    if (rc >= 0) {
        rc = user_strncpy(newp, unew, sizeof(newp));
    }
    if (rc < 0) {
        return (uint64_t)rc;
    }
    return (uint64_t)vfs_link(oldp, newp);
}

static uint64_t sys_rename(uint64_t uold, uint64_t unew) {
    char oldp[VFS_PATH_MAX];
    char newp[VFS_PATH_MAX];
    long rc = user_strncpy(oldp, uold, sizeof(oldp));
    if (rc >= 0) {
        rc = user_strncpy(newp, unew, sizeof(newp));
    }
    if (rc < 0) {
        return (uint64_t)rc;
    }
    return (uint64_t)vfs_rename(oldp, newp);
}

static uint64_t sys_getdents64(uint64_t fd, uint64_t dirp, uint64_t count) {
    struct file *f = process_file_get((int)fd);
    if (f == NULL) {
        return ERR(EBADF);
    }
    if (f->ops->getdents == NULL) {
        return ERR(ENOTDIR);
    }
    if (!user_range_ok(dirp, count, 1)) {
        return ERR(EFAULT);
    }
    return (uint64_t)f->ops->getdents(f, (void *)dirp, count);
}

void syscall_dispatch(struct syscall_frame *frame) {
    switch (frame->rax) {
    case SYS_read:
        frame->rax = sys_read(frame->rdi, frame->rsi, frame->rdx);
        return;
    case SYS_write:
        frame->rax = sys_write(frame->rdi, frame->rsi, frame->rdx);
        return;
    case SYS_open:
        frame->rax = sys_open(frame->rdi, frame->rsi, frame->rdx);
        return;
    case SYS_close:
        frame->rax = sys_close(frame->rdi);
        return;
    case SYS_fstat:
        frame->rax = sys_fstat(frame->rdi, frame->rsi);
        return;
    case SYS_lseek:
        frame->rax = sys_lseek(frame->rdi, frame->rsi, frame->rdx);
        return;
    case SYS_getpid:
        frame->rax = (uint64_t)process_getpid();
        return;
    case SYS_fork:
        frame->rax = (uint64_t)process_fork(frame);
        return;
    case SYS_execve:
        frame->rax = (uint64_t)process_execve(frame, frame->rdi, frame->rsi, frame->rdx);
        return;
    case SYS_wait4:
        frame->rax =
            (uint64_t)process_wait4((int)frame->rdi, frame->rsi, (int)frame->rdx, frame->r10);
        return;
    case SYS_fsync:
        frame->rax = sys_fsync(frame->rdi);
        return;
    case SYS_rename:
        frame->rax = sys_rename(frame->rdi, frame->rsi);
        return;
    case SYS_mkdir:
        frame->rax = sys_mkdir(frame->rdi, frame->rsi);
        return;
    case SYS_rmdir:
        frame->rax = sys_rmdir(frame->rdi);
        return;
    case SYS_link:
        frame->rax = sys_link(frame->rdi, frame->rsi);
        return;
    case SYS_unlink:
        frame->rax = sys_unlink(frame->rdi);
        return;
    case SYS_getdents64:
        frame->rax = sys_getdents64(frame->rdi, frame->rsi, frame->rdx);
        return;
    case SYS_exit:
        process_exit((int)frame->rdi);
    default:
        frame->rax = ERR(ENOSYS);
        return;
    }
}
