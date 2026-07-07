/*
 * proc_core.c - pure process table state machine (see proc_core.h).
 *
 * All functions are total over valid inputs; state-machine
 * violations (exiting a non-live pid, reaping a non-zombie) are
 * caller bugs and the kernel layer guards them with KASSERT before
 * calling in. Pure code stays assertion-free so the host tests can
 * probe every path.
 */
#include <proc_core.h>

#include <stddef.h>

void proc_table_init(struct proc_table *pt) {
    for (int i = 0; i < PROC_MAX; i++) {
        pt->procs[i].pid = 0;
        pt->procs[i].ppid = 0;
        pt->procs[i].state = PROC_UNUSED;
        pt->procs[i].exit_status = 0;
    }
    pt->next_pid = PROC_INIT_PID;
    pt->nprocs = 0;
}

int proc_alloc(struct proc_table *pt, int ppid) {
    for (int i = 0; i < PROC_MAX; i++) {
        struct proc_entry *p = &pt->procs[i];
        if (p->state != PROC_UNUSED) {
            continue;
        }
        p->pid = pt->next_pid++;
        p->ppid = ppid;
        p->state = PROC_LIVE;
        p->exit_status = 0;
        pt->nprocs++;
        return p->pid;
    }
    return 0;
}

struct proc_entry *proc_find(struct proc_table *pt, int pid) {
    if (pid <= 0) {
        return NULL;
    }
    for (int i = 0; i < PROC_MAX; i++) {
        if (pt->procs[i].state != PROC_UNUSED && pt->procs[i].pid == pid) {
            return &pt->procs[i];
        }
    }
    return NULL;
}

int proc_exit(struct proc_table *pt, int pid, int status) {
    struct proc_entry *p = proc_find(pt, pid);
    if (p == NULL || p->state != PROC_LIVE) {
        return 0;
    }
    p->state = PROC_ZOMBIE;
    p->exit_status = status;
    /* Orphans go to init, so every zombie has a live reaper. */
    for (int i = 0; i < PROC_MAX; i++) {
        if (pt->procs[i].state != PROC_UNUSED && pt->procs[i].ppid == pid) {
            pt->procs[i].ppid = PROC_INIT_PID;
        }
    }
    return p->ppid;
}

int proc_wait_find(struct proc_table *pt, int ppid, int pid_arg, int *status_out) {
    int have_child = 0;
    for (int i = 0; i < PROC_MAX; i++) {
        struct proc_entry *p = &pt->procs[i];
        if (p->state == PROC_UNUSED || p->ppid != ppid) {
            continue;
        }
        if (pid_arg > 0 && p->pid != pid_arg) {
            continue;
        }
        have_child = 1;
        if (p->state == PROC_ZOMBIE) {
            if (status_out != NULL) {
                *status_out = p->exit_status;
            }
            return p->pid;
        }
    }
    return have_child ? PROC_WAIT_BLOCK : PROC_WAIT_NOCHILD;
}

void proc_reap(struct proc_table *pt, int pid) {
    struct proc_entry *p = proc_find(pt, pid);
    if (p == NULL || p->state != PROC_ZOMBIE) {
        return;
    }
    p->pid = 0;
    p->ppid = 0;
    p->state = PROC_UNUSED;
    p->exit_status = 0;
    pt->nprocs--;
}

int proc_count(const struct proc_table *pt) {
    return pt->nprocs;
}
