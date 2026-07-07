/*
 * proc_core.h - pure process table state machine.
 *
 * Plain C data and logic only (no kernel dependencies) so the
 * pid/parent/zombie bookkeeping — the error-prone part of
 * fork/exit/wait — is unit-tested on the host
 * (tests/host/test_proc.c). The kernel side (kernel/proc/process.c)
 * wraps this with address spaces, hosting threads, and blocking.
 *
 * Lifecycle: proc_alloc() -> PROC_LIVE -> proc_exit() -> PROC_ZOMBIE
 * (exit status held for the parent) -> proc_reap() -> slot reused.
 * A dying process's children are reparented to init (pid 1), Unix
 * style, so every zombie always has a live parent to reap it.
 */
#pragma once

#define PROC_MAX      64
#define PROC_INIT_PID 1

enum proc_state {
    PROC_UNUSED = 0,
    PROC_LIVE,
    PROC_ZOMBIE,
};

struct proc_entry {
    int pid; /* 0 while the slot is unused */
    int ppid;
    enum proc_state state;
    int exit_status;
};

struct proc_table {
    struct proc_entry procs[PROC_MAX];
    int next_pid;
    int nprocs; /* live + zombie */
};

void proc_table_init(struct proc_table *pt);

/* Allocate a slot with a fresh pid (never reused within a boot).
 * The first allocation is init and gets PROC_INIT_PID. Returns the
 * pid, or 0 when the table is full. */
int proc_alloc(struct proc_table *pt, int ppid);

/* The entry for `pid`, or NULL if no live/zombie process has it. */
struct proc_entry *proc_find(struct proc_table *pt, int pid);

/* PROC_LIVE -> PROC_ZOMBIE: record the exit status and reparent the
 * dying process's children to init. Returns the (pre-exit) ppid. */
int proc_exit(struct proc_table *pt, int pid, int status);

/*
 * Wait matching for a parent: pid_arg > 0 selects that child
 * exactly, -1 selects any child (other values are the kernel
 * layer's problem). Returns the pid of a reapable zombie child
 * (status stored in *status_out), PROC_WAIT_BLOCK when matching
 * children exist but none has exited, or PROC_WAIT_NOCHILD.
 */
#define PROC_WAIT_BLOCK   0
#define PROC_WAIT_NOCHILD (-1)
int proc_wait_find(struct proc_table *pt, int ppid, int pid_arg, int *status_out);

/* PROC_ZOMBIE -> PROC_UNUSED: release the slot. */
void proc_reap(struct proc_table *pt, int pid);

/* Live + zombie processes, for diagnostics and leak checks. */
int proc_count(const struct proc_table *pt);
