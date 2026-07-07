/*
 * process.h - user processes.
 *
 * Phase 4 process model: every process is a user address space
 * hosted by one kernel thread. The pid/parent/zombie bookkeeping is
 * the pure proc_core state machine (host-tested); this layer adds
 * address spaces, ELF loading, the argv/envp stack, blocking wait,
 * and teardown. Programs come from a built-in table of ELF images
 * embedded in the kernel until the VFS lands (Phase 5).
 */
#pragma once

#include <stdint.h>

#include <compiler.h>

struct syscall_frame;

/*
 * Launch init ("/bin/init" from the built-in program table), wait
 * for it — and transitively everything it forked — to exit, and
 * tear the process table down. Requires the scheduler running and
 * syscall_init() done. Panics if init cannot be built, exits
 * leaving processes behind, or leaks frames.
 */
void process_run_init(void);

/* Terminate the calling process (from SYS_exit): zombie in the
 * table, parent woken, hosting thread ends. */
NORETURN void process_exit(int status);

/* fork(): clone the calling process — address space copied eagerly,
 * user context copied from the parent's syscall frame with rax = 0.
 * Returns the child pid, -EAGAIN (table full) or -ENOMEM. */
long process_fork(const struct syscall_frame *parent_frame);

/*
 * execve(path, argv, envp): replace the calling process's image with
 * a program from the built-in table. On success the syscall frame is
 * rewritten to enter the fresh image (so "returning" 0 lands at its
 * entry point); on error the old image is untouched. argv/envp are
 * NULL-terminated arrays of user string pointers; NULL means empty.
 */
long process_execve(struct syscall_frame *frame, uint64_t upath, uint64_t uargv, uint64_t uenvp);

/*
 * wait4(pid, wstatus, options, rusage): reap a zombie child (pid > 0
 * exact, -1 any), blocking until one exits. Writes the Linux wait
 * status encoding ((exit_status & 0xff) << 8) to *wstatus when
 * non-NULL. options must be 0 and rusage NULL (no process groups or
 * accounting yet). Returns the reaped pid, -ECHILD, -EINVAL or
 * -EFAULT.
 */
long process_wait4(int pid_arg, uint64_t uwstatus, int options, uint64_t urusage);

/* The calling thread's process id (0 for pure kernel threads). */
int process_getpid(void);
