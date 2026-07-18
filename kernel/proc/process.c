/*
 * process.c - the process model: fork, execve, wait4, exit.
 *
 * Structure: proc_core.c owns the pid/parent/zombie state machine
 * (pure, host-tested); this file supplies everything only the kernel
 * can — address spaces, hosting threads, the SysV argv/envp stack,
 * blocking, and teardown. Kernel-side state lives in pstate[],
 * parallel to the proc_core slots.
 *
 * User memory layout for every process:
 *
 *   PT_LOAD segments wherever the ELF says (user.ld links 0x400000)
 *   0x00007FFFFFFFB000  stack, 4 pages RW + NX
 *   initial RSP just below 0x00007FFFFFFFF000: [argc][argv...][envp...]
 *
 * Concurrency: single CPU; the process table and pstate[] are
 * mutated under cpu_irq_save sections. The lost-wakeup race in wait4
 * is closed by publishing the waiter and blocking inside one
 * interrupts-off section (sched_block contract).
 */
#include <process.h>

#include <stddef.h>

#include <arch/x86_64/cpu.h>
#include <arch/x86_64/paging.h>
#include <arch/x86_64/usermode.h>
#include <elf64.h>
#include <errno.h>
#include <kmalloc.h>
#include <kprintf.h>
#include <memlayout.h>
#include <panic.h>
#include <pmm.h>
#include <proc_core.h>
#include <sched.h>
#include <string.h>
#include <syscall.h>
#include <uaccess.h>
#include <vfs.h>
#include <vmm.h>

#define USER_STACK_TOP   0x00007FFFFFFFF000ull
#define USER_STACK_PAGES 4u
#define USER_RFLAGS      0x202ull /* IF | reserved-1 */

/* execve limits (per vector; also bounded by the stack page fit). */
#define ARG_MAX_COUNT 16
#define ARG_MAX_BYTES 128

/* Kernel-side per-process state, parallel to ptable.procs[]. */
struct process {
    struct addrspace as;
    struct thread *host;        /* kernel thread hosting ring 3 */
    struct thread *waiter;      /* thread of *this* process blocked in wait4 */
    struct syscall_frame start; /* user context for the first ring 3 entry */
    struct file *fd[FD_MAX];    /* open files; NULL = free slot */
    char name[32];              /* program path, for thread naming */
};

static struct proc_table ptable;
static struct process pstate[PROC_MAX];

static struct process *pstate_of(int pid) {
    struct proc_entry *e = proc_find(&ptable, pid);
    KASSERT(e != NULL);
    return &pstate[e - ptable.procs];
}

/* Bounded copy, always NUL-terminated (truncates long names). */
static void name_set(char *dst, const char *src, size_t cap) {
    size_t n = strnlen(src, cap - 1);
    memcpy(dst, src, n);
    dst[n] = '\0';
}

/*
 * Build the SysV process-entry stack in `as` (inactive is fine): the
 * strings and the [argc][argv][NULL][envp][NULL][auxv AT_NULL] block
 * in the top stack page, 16-byte-aligned RSP pointing at argc.
 * argv/envp are kernel-side NULL-terminated vectors.
 */
static long stack_build(struct addrspace *as, const char *const argv[], int argc,
                        const char *const envp[], int envc, uint64_t *rsp_out) {
    uint64_t page_va = USER_STACK_TOP - PAGE_SIZE;
    uint8_t *page = kzalloc(PAGE_SIZE);
    if (page == NULL) {
        return -ENOMEM;
    }

    /* Strings first, packed downward from the stack top. */
    uint64_t str_uaddr[2 * ARG_MAX_COUNT];
    uint64_t cursor = PAGE_SIZE;
    int nstr = 0;
    for (int pass = 0; pass < 2; pass++) {
        const char *const *vec = (pass == 0) ? argv : envp;
        int n = (pass == 0) ? argc : envc;
        for (int i = 0; i < n; i++) {
            uint64_t len = strlen(vec[i]) + 1;
            if (len > cursor) {
                kfree(page);
                return -E2BIG;
            }
            cursor -= len;
            memcpy(page + cursor, vec[i], len);
            str_uaddr[nstr++] = page_va + cursor;
        }
    }

    /* Vector block: argc, argv[], NULL, envp[], NULL, AT_NULL pair. */
    uint64_t nvec = 1 + (uint64_t)argc + 1 + (uint64_t)envc + 1 + 2;
    uint64_t rsp = (page_va + cursor - (nvec * 8)) & ~0xFull;
    if (rsp < page_va + 64) { /* keep clear of the page base */
        kfree(page);
        return -E2BIG;
    }
    uint64_t *vec = (uint64_t *)(page + (rsp - page_va));
    int v = 0;
    vec[v++] = (uint64_t)argc;
    for (int i = 0; i < argc; i++) {
        vec[v++] = str_uaddr[i];
    }
    vec[v++] = 0;
    for (int i = 0; i < envc; i++) {
        vec[v++] = str_uaddr[argc + i];
    }
    vec[v++] = 0;
    vec[v++] = 0; /* auxv: AT_NULL */
    vec[v++] = 0;

    /* Install as the top stack page's contents. */
    uint64_t phys = 0;
    uint64_t pte = paging_lookup(as, page_va, &phys);
    KASSERT((pte & PTE_P) != 0);
    memcpy(phys_to_virt(phys & PTE_ADDR_MASK), page, PAGE_SIZE);
    kfree(page);
    *rsp_out = rsp;
    return 0;
}

/*
 * Build a complete user image in a fresh address space: create it,
 * load the ELF (`image`, `size` bytes of kernel memory), map the
 * stack, lay out argv/envp, and fill `start` with the entry
 * context. On error the address space is destroyed and -errno
 * returned; nothing else is touched.
 */
static long image_build(struct addrspace *as, const uint8_t *image, uint64_t size,
                        const char *const argv[], int argc, const char *const envp[], int envc,
                        struct syscall_frame *start) {
    if (vmm_addrspace_create_user(as) != 0) {
        return -ENOMEM;
    }
    uint64_t entry = 0;
    int err = elf64_load(as, image, size, &entry);
    if (err != ELF64_OK) {
        paging_user_destroy(as);
        return (err == ELF64_ENOMEM) ? -ENOMEM : -ENOEXEC;
    }
    for (unsigned i = 1; i <= USER_STACK_PAGES; i++) {
        uint64_t frame = pmm_alloc_frame();
        if (frame == 0) {
            paging_user_destroy(as);
            return -ENOMEM;
        }
        memset(phys_to_virt(frame), 0, PAGE_SIZE);
        uint64_t va = USER_STACK_TOP - ((uint64_t)i * PAGE_SIZE);
        if (paging_map_4k(as, va, frame, PTE_US | PTE_W | PTE_NX) != PAGING_OK) {
            pmm_free_frame(frame);
            paging_user_destroy(as);
            return -ENOMEM;
        }
    }
    uint64_t rsp = 0;
    long rc = stack_build(as, argv, argc, envp, envc, &rsp);
    if (rc != 0) {
        paging_user_destroy(as);
        return rc;
    }
    memset(start, 0, sizeof(*start));
    start->rip = entry;
    start->rsp = rsp;
    start->rflags = USER_RFLAGS;
    return 0;
}

/* Entry of every hosting kernel thread: bind the address space and
 * enter ring 3 with the prepared context. */
static void proc_host_entry(void *arg) {
    struct process *p = arg;
    sched_set_addrspace(&p->as);
    user_frame_enter(&p->start);
}

/* Create the hosting thread with its pid attached before it can run. */
static void host_launch(struct process *p, int pid) {
    uint64_t flags = cpu_irq_save();
    p->host = thread_create(p->name, proc_host_entry, p);
    p->host->pid = pid;
    cpu_irq_restore(flags);
}

int process_getpid(void) {
    return thread_current()->pid;
}

/* ---- the per-process fd table ----
 *
 * Single CPU and no fd sharing between threads (one thread per
 * process) make these plain array operations: nothing can race a
 * table entry while its owner is inside a syscall. */

struct file *process_file_get(int fd) {
    int pid = thread_current()->pid;
    if (pid == 0 || fd < 0 || fd >= FD_MAX) {
        return NULL;
    }
    return pstate_of(pid)->fd[fd];
}

long process_fd_install(struct file *f) {
    struct process *p = pstate_of(thread_current()->pid);
    for (int i = 0; i < FD_MAX; i++) {
        if (p->fd[i] == NULL) {
            p->fd[i] = f;
            return i;
        }
    }
    return -EMFILE;
}

long process_fd_close(int fd) {
    struct file *f = process_file_get(fd);
    if (f == NULL) {
        return -EBADF;
    }
    pstate_of(thread_current()->pid)->fd[fd] = NULL;
    vfs_file_put(f);
    return 0;
}

static void fd_table_close_all(struct process *p) {
    for (int i = 0; i < FD_MAX; i++) {
        if (p->fd[i] != NULL) {
            vfs_file_put(p->fd[i]);
            p->fd[i] = NULL;
        }
    }
}

/*
 * The one way out of a process: record the Linux wait status in the
 * table, wake a parent blocked in wait4, and end the hosting thread.
 * The table holds the *encoded* wstatus (WIFEXITED/WIFSIGNALED
 * vocabulary), decided by the caller.
 */
static NORETURN void proc_die(int wstatus) {
    int pid = thread_current()->pid;
    KASSERT(pid > 0);
    /* Release the process's files first (refcount drops; kfree is
     * interrupt-safe, so the trap-path caller is fine too). */
    fd_table_close_all(pstate_of(pid));
    uint64_t flags = cpu_irq_save();
    int ppid = proc_exit(&ptable, pid, wstatus);
    if (ppid > 0) {
        struct proc_entry *parent = proc_find(&ptable, ppid);
        if (parent != NULL && parent->state == PROC_LIVE) {
            struct process *pp = &pstate[parent - ptable.procs];
            if (pp->waiter != NULL) {
                struct thread *w = pp->waiter;
                pp->waiter = NULL;
                sched_wake(w);
            }
        }
    }
    (void)flags; /* the thread dies; interrupt state dies with it */
    thread_exit();
}

NORETURN void process_exit(int status) {
    proc_die((status & 0xFF) << 8); /* WIFEXITED encoding */
}

NORETURN void process_kill(int sig) {
    int pid = thread_current()->pid;
    KASSERT(pid > 0);
    kprintf("user: pid %d (%s) killed by signal %d\n", pid, pstate_of(pid)->name, sig);
    proc_die(sig & 0x7F); /* WIFSIGNALED encoding */
}

long process_fork(const struct syscall_frame *parent_frame) {
    struct thread *me = thread_current();
    KASSERT(me->pid > 0 && me->as != NULL);

    uint64_t flags = cpu_irq_save();
    int pid = proc_alloc(&ptable, me->pid);
    cpu_irq_restore(flags);
    if (pid == 0) {
        return -EAGAIN;
    }
    struct process *cp = pstate_of(pid);
    memcpy(cp->name, pstate_of(me->pid)->name, sizeof(cp->name));
    cp->waiter = NULL;

    if (vmm_addrspace_create_user(&cp->as) != 0 ||
        paging_user_clone(&cp->as, me->as) != PAGING_OK) {
        if (cp->as.pml4_phys != 0) {
            paging_user_destroy(&cp->as);
        }
        flags = cpu_irq_save();
        proc_exit(&ptable, pid, 0); /* never ran: no waiter to wake */
        proc_reap(&ptable, pid);
        cpu_irq_restore(flags);
        return -ENOMEM;
    }

    /* Share the parent's open files: same descriptions, same
     * offsets — the POSIX fork contract. */
    struct process *pp = pstate_of(me->pid);
    for (int i = 0; i < FD_MAX; i++) {
        cp->fd[i] = pp->fd[i];
        if (cp->fd[i] != NULL) {
            vfs_file_ref(cp->fd[i]);
        }
    }

    /* The child is the parent at the syscall return point, but fork
     * returned 0 to it. */
    cp->start = *parent_frame;
    cp->start.rax = 0;

    host_launch(cp, pid);
    return pid;
}

/* Copy a user argv/envp-style vector (NULL-terminated array of
 * string pointers; NULL vector = empty) into pack->vec. */
struct argpack {
    char bufs[ARG_MAX_COUNT][ARG_MAX_BYTES];
    const char *vec[ARG_MAX_COUNT + 1];
    int count;
};

static long argpack_copy(struct argpack *pack, uint64_t uvec) {
    pack->count = 0;
    pack->vec[0] = NULL;
    if (uvec == 0) {
        return 0;
    }
    for (int i = 0;; i++) {
        uint64_t uptr = 0;
        if (user_copy_from(&uptr, uvec + ((uint64_t)i * 8), 8) != 0) {
            return -EFAULT;
        }
        if (uptr == 0) {
            break;
        }
        if (i == ARG_MAX_COUNT) {
            return -E2BIG;
        }
        long n = user_strncpy(pack->bufs[i], uptr, ARG_MAX_BYTES);
        if (n == -EFAULT) {
            return -EFAULT;
        }
        if (n < 0) {
            return -E2BIG;
        }
        pack->vec[i] = pack->bufs[i];
        pack->count = i + 1;
    }
    pack->vec[pack->count] = NULL;
    return 0;
}

/* Heap-held execve scratch: two argpacks are too big for a 16 KiB
 * kernel stack. */
struct execve_args {
    char path[VFS_PATH_MAX];
    struct argpack argv;
    struct argpack envp;
};

long process_execve(struct syscall_frame *frame, uint64_t upath, uint64_t uargv, uint64_t uenvp) {
    struct thread *me = thread_current();
    KASSERT(me->pid > 0 && me->as != NULL);
    struct process *p = pstate_of(me->pid);

    struct execve_args *xa = kzalloc(sizeof(*xa));
    if (xa == NULL) {
        return -ENOMEM;
    }
    long rc = user_strncpy(xa->path, upath, sizeof(xa->path));
    if (rc < 0) {
        rc = (rc == -EFAULT) ? -EFAULT : -ENAMETOOLONG;
        goto out;
    }
    rc = argpack_copy(&xa->argv, uargv);
    if (rc != 0) {
        goto out;
    }
    rc = argpack_copy(&xa->envp, uenvp);
    if (rc != 0) {
        goto out;
    }

    /* Pull the whole ELF off the disk, then build the new image
     * completely before touching the old one, so every failure
     * leaves the caller exactly as it was. */
    void *image = NULL;
    uint64_t size = 0;
    rc = vfs_read_file(xa->path, &image, &size);
    if (rc != 0) {
        goto out;
    }
    struct addrspace new_as;
    struct syscall_frame start;
    rc = image_build(&new_as, image, size, xa->argv.vec, xa->argv.count, xa->envp.vec,
                     xa->envp.count, &start);
    kfree(image);
    if (rc != 0) {
        goto out;
    }

    /* Commit: swap address spaces under the thread's feet. The struct
     * address stays the same, so CR3 must be reloaded by hand. */
    uint64_t flags = cpu_irq_save();
    struct addrspace old_as = p->as;
    p->as = new_as;
    paging_activate(&p->as);
    cpu_irq_restore(flags);
    paging_user_destroy(&old_as);

    name_set(p->name, xa->path, sizeof(p->name));

    /* "Return" into the fresh image: the sysret path restores this
     * frame, landing at the ELF entry with a SysV argv stack. The
     * fd table deliberately survives (no close-on-exec flags yet). */
    *frame = start;
    rc = 0;

out:
    kfree(xa);
    return rc;
}

long process_wait4(int pid_arg, uint64_t uwstatus, int options, uint64_t urusage) {
    if (options != 0 || urusage != 0) {
        return -EINVAL; /* no WNOHANG/process groups/rusage yet */
    }
    if (pid_arg == 0 || pid_arg < -1) {
        return -EINVAL; /* process groups do not exist */
    }
    struct thread *me = thread_current();
    KASSERT(me->pid > 0);
    struct process *mp = pstate_of(me->pid);

    for (;;) {
        uint64_t flags = cpu_irq_save();
        int status = 0;
        int r = proc_wait_find(&ptable, me->pid, pid_arg, &status);
        if (r == PROC_WAIT_NOCHILD) {
            cpu_irq_restore(flags);
            return -ECHILD;
        }
        if (r > 0) {
            struct thread *host = pstate_of(r)->host;
            cpu_irq_restore(flags);

            /* The child is a zombie in the table; collect its thread
             * and memory, then release the pid. */
            thread_join(host);
            paging_user_destroy(&pstate_of(r)->as);
            flags = cpu_irq_save();
            proc_reap(&ptable, r);
            cpu_irq_restore(flags);

            if (uwstatus != 0) {
                /* The table already holds the encoded wstatus. */
                if (user_copy_to(uwstatus, &status, sizeof(status)) != 0) {
                    return -EFAULT;
                }
            }
            return r;
        }
        /* Children exist but none has exited: block until one does.
         * Publishing the waiter and blocking inside one interrupts-off
         * section closes the lost-wakeup race. */
        KASSERT(mp->waiter == NULL);
        mp->waiter = me;
        sched_block();
        cpu_irq_restore(flags);
    }
}

void process_run_init(void) {
    proc_table_init(&ptable);
    static const char *const INIT_PATH = "/bin/init";

    uint64_t frames_before = pmm_free_frames();
    int pid = proc_alloc(&ptable, 0);
    KASSERT(pid == PROC_INIT_PID);
    struct process *p = pstate_of(pid);
    name_set(p->name, INIT_PATH, sizeof(p->name));
    p->waiter = NULL;

    void *image = NULL;
    uint64_t size = 0;
    long rc = vfs_read_file(INIT_PATH, &image, &size);
    if (rc != 0) {
        panic("process: cannot read %s: %lld", INIT_PATH, (long long)rc);
    }
    const char *const argv[] = {INIT_PATH, NULL};
    rc = image_build(&p->as, image, size, argv, 1, NULL, 0, &p->start);
    kfree(image);
    if (rc != 0) {
        panic("process: cannot build init: %lld", (long long)rc);
    }

    /* Init starts with the console on 0/1/2: one description, three
     * references — exactly what a login shell would inherit. */
    struct file *con = NULL;
    rc = vfs_open("/dev/console", O_RDWR, 0, &con);
    if (rc != 0) {
        panic("process: cannot open /dev/console: %lld", (long long)rc);
    }
    p->fd[0] = con;
    vfs_file_ref(con);
    p->fd[1] = con;
    vfs_file_ref(con);
    p->fd[2] = con;

    kprintf("user: launching init (%s from disk, %llu bytes)\n", INIT_PATH,
            (unsigned long long)size);
    host_launch(p, pid);
    thread_join(p->host);

    /* Init is a zombie now; everything it forked must already be
     * reaped, or the system lost track of a process. */
    struct proc_entry *e = proc_find(&ptable, pid);
    KASSERT(e != NULL && e->state == PROC_ZOMBIE);
    if (proc_count(&ptable) != 1) {
        panic("process: init exited leaving %d processes", proc_count(&ptable) - 1);
    }
    int wstatus = e->exit_status;
    paging_user_destroy(&p->as);
    proc_reap(&ptable, pid);

    if (pmm_free_frames() != frames_before) {
        panic("process: init leaked %lld frames", (long long)(frames_before - pmm_free_frames()));
    }
    if ((wstatus & 0x7F) != 0) { /* WIFSIGNALED: init must never fault */
        panic("process: init killed by signal %d", wstatus & 0x7F);
    }
    kprintf("user: init exited (status %d)\n", wstatus >> 8);
}
