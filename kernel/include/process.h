/*
 * process.h - user processes.
 *
 * Every process is a user address space hosted by one kernel
 * thread. The pid/parent/zombie bookkeeping is the pure proc_core
 * state machine (host-tested); this layer adds address spaces, ELF
 * loading through the VFS, the argv/envp stack, per-process file
 * descriptor tables, blocking wait, and teardown.
 *
 * File descriptors: each process owns a small table of pointers to
 * VFS open-file descriptions. fork() duplicates the table (both
 * processes share each description, offsets included — POSIX);
 * execve() leaves it untouched; exit closes everything. Init starts
 * with 0/1/2 all referencing one open of /dev/console.
 */
#pragma once

#include <stdint.h>

#include <compiler.h>

struct file;
struct syscall_frame;

/* Per-process fd table size. Enough for init's acceptance tests and
 * early userspace; grows when something real runs out. */
#define FD_MAX 16

/*
 * Launch init from /bin/init on the root filesystem, wait for it —
 * and transitively everything it forked — to exit, and tear the
 * process table down. Requires the scheduler, syscall_init(), and
 * vfs_init(). Panics if init cannot be loaded, exits leaving
 * processes behind, or leaks frames.
 */
void process_run_init(void);

/*
 * fd-table access for the syscall layer. All three operate on the
 * calling process. process_file_get returns a borrowed pointer
 * (valid for the syscall: the table entry cannot be closed under it
 * on one CPU) or NULL for a bad/closed fd; install takes over one
 * reference and returns the new fd or -EMFILE; close drops the
 * table's reference and returns 0 or -EBADF.
 */
struct file *process_file_get(int fd);
long process_fd_install(struct file *f);
long process_fd_close(int fd);

/* Terminate the calling process (from SYS_exit): zombie in the
 * table, parent woken, hosting thread ends. The wait status is the
 * Linux "exited" encoding, (status & 0xff) << 8. */
NORETURN void process_exit(int status);

/* Terminate the calling process because of a hardware fault (called
 * from the trap path with interrupts off). Same exit machinery as
 * process_exit, but the wait status is the Linux "killed by signal"
 * encoding, sig & 0x7f, and a diagnostic names the victim. */
NORETURN void process_kill(int sig);

/* fork(): clone the calling process — address space copied eagerly,
 * fd table duplicated (descriptions shared), user context copied
 * from the parent's syscall frame with rax = 0. Returns the child
 * pid, -EAGAIN (table full) or -ENOMEM. */
long process_fork(const struct syscall_frame *parent_frame);

/*
 * execve(path, argv, envp): replace the calling process's image with
 * the ELF at `path` on the filesystem. On success the syscall frame
 * is rewritten to enter the fresh image (so "returning" 0 lands at
 * its entry point); on error the old image is untouched. File
 * descriptors survive the exec. argv/envp are NULL-terminated
 * arrays of user string pointers; NULL means empty.
 */
long process_execve(struct syscall_frame *frame, uint64_t upath, uint64_t uargv, uint64_t uenvp);

/*
 * wait4(pid, wstatus, options, rusage): reap a zombie child (pid > 0
 * exact, -1 any), blocking until one exits. Writes the Linux wait
 * status to *wstatus when non-NULL: (code & 0xff) << 8 for a normal
 * exit, the signal number for a fault kill (WIFEXITED/WIFSIGNALED
 * semantics). options must be 0 and rusage NULL (no process groups
 * or accounting yet). Returns the reaped pid, -ECHILD, -EINVAL or
 * -EFAULT.
 */
long process_wait4(int pid_arg, uint64_t uwstatus, int options, uint64_t urusage);

/* The calling thread's process id (0 for pure kernel threads). */
int process_getpid(void);
