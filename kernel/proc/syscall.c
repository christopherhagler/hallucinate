/*
 * syscall.c - system call dispatch and the small implementations.
 *
 * ABI in syscall.h. User pointers go through uaccess.h validation;
 * the process-model syscalls (fork/execve/wait4/exit) live in
 * process.c and get the syscall frame where they need the full user
 * context.
 */
#include <syscall.h>

#include <stddef.h>

#include <console.h>
#include <errno.h>
#include <process.h>
#include <uaccess.h>

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

/* write(fd, buf, count): fd 1 (stdout) and 2 (stderr) both reach the
 * kernel console until file descriptors exist (Phase 5). */
static uint64_t sys_write(uint64_t fd, uint64_t buf, uint64_t count) {
    if (fd != 1 && fd != 2) {
        return ERR(EBADF);
    }
    if (count == 0) {
        return 0;
    }
    if (!user_range_ok(buf, count, 0)) {
        return ERR(EFAULT);
    }
    console_write((const char *)buf, count);
    return count;
}

void syscall_dispatch(struct syscall_frame *frame) {
    switch (frame->rax) {
    case SYS_write:
        frame->rax = sys_write(frame->rdi, frame->rsi, frame->rdx);
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
    case SYS_exit:
        process_exit((int)frame->rdi);
    default:
        frame->rax = ERR(ENOSYS);
        return;
    }
}
