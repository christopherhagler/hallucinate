/*
 * pipe.c - anonymous pipes: two file descriptions over one shared
 * ring buffer, unattached to any mount.
 *
 * The ring itself is pipe_core.c, pure and host-tested; this file
 * adds the parts a byte buffer can't provide alone: blocking when a
 * reader finds the buffer empty or a writer finds it full, and the
 * reader/writer-count bookkeeping that turns "the other end closed"
 * into EOF (read) or -EPIPE (write).
 *
 * Locking follows the PMM/heap pattern, not the VFS's: a pipe
 * operation is pure memory copying, not disk I/O, so there is nothing
 * to hold a sleeping lock across. Every critical section is a plain
 * interrupts-off region (cpu_irq_save/restore) — the same discipline
 * that makes it safe to call sched_block() from inside one: the wait
 * is published (this thread pushed onto a wait queue) and the block
 * happens without interrupts re-enabling in between, so a wakeup can
 * never be lost between the check and the block (the same contract
 * devfs's console read and wait4 rely on). The wait queues reuse
 * thread->next, exactly as struct mutex's does — a thread blocked
 * here is provably on no other queue.
 *
 * Write semantics: write(2) blocks until every requested byte is
 * queued, looping internally and re-blocking as space frees up — it
 * does not report a short write just because the buffer was smaller
 * than the request, matching Linux for writes that fit within one
 * pipe lifetime. Whether a write is atomic with respect to other
 * writers (as Linux guarantees for writes up to PIPE_BUF) is not
 * promised here: nothing in this kernel has more than one writer on
 * the same pipe yet, so the guarantee has no observer to violate it
 * for; revisit if that changes.
 */
#include <vfs.h>

#include <stddef.h>
#include <stdint.h>

#include <arch/x86_64/cpu.h>
#include <errno.h>
#include <kmalloc.h>
#include <panic.h>
#include <pipe_core.h>
#include <sched.h>
#include <string.h>

#define PIPE_CAPACITY 4096u

/* st_dev for pipes: distinct from graphfs (1) and devfs (2). Pipes
 * have no path and no directory entry, so uniqueness against those
 * two is all "device" needs to mean here. */
#define PIPE_STDEV 3u

struct pipe {
    struct pipe_core core;
    uint8_t buf[PIPE_CAPACITY];
    int readers;
    int writers;
    struct thread *read_wait_head, *read_wait_tail;
    struct thread *write_wait_head, *write_wait_tail;
};

/* ---- FIFO wait-queue helpers (thread->next), mirroring mutex.c ---- */

static void wq_push(struct thread **head, struct thread **tail, struct thread *t) {
    t->next = NULL;
    if (*tail != NULL) {
        (*tail)->next = t;
    } else {
        *head = t;
    }
    *tail = t;
}

/* Dequeue and wake every waiter currently queued. */
static void wq_wake_all(struct thread **head, struct thread **tail) {
    struct thread *t = *head;
    *head = NULL;
    *tail = NULL;
    while (t != NULL) {
        struct thread *next = t->next;
        t->next = NULL;
        sched_wake(t);
        t = next;
    }
}

/* Dequeue and wake exactly one waiter, if any. */
static void wq_wake_one(struct thread **head, struct thread **tail) {
    struct thread *t = *head;
    if (t == NULL) {
        return;
    }
    *head = t->next;
    if (*head == NULL) {
        *tail = NULL;
    }
    t->next = NULL;
    sched_wake(t);
}

/* ---- file_ops ---- */

static long pipe_read(struct file *f, void *buf, size_t len) {
    struct pipe *p = f->priv;
    if (len == 0) {
        return 0;
    }
    for (;;) {
        uint64_t flags = cpu_irq_save();
        size_t n = pipe_core_read(&p->core, buf, len);
        if (n > 0) {
            wq_wake_one(&p->write_wait_head, &p->write_wait_tail);
            cpu_irq_restore(flags);
            return (long)n;
        }
        if (p->writers == 0) {
            cpu_irq_restore(flags); /* no data, no writers left: EOF */
            return 0;
        }
        wq_push(&p->read_wait_head, &p->read_wait_tail, thread_current());
        sched_block();
        cpu_irq_restore(flags);
    }
}

static long pipe_write(struct file *f, const void *buf, size_t len) {
    struct pipe *p = f->priv;
    if (len == 0) {
        return 0;
    }
    const uint8_t *src = buf;
    size_t done = 0;
    while (done < len) {
        uint64_t flags = cpu_irq_save();
        if (p->readers == 0) {
            cpu_irq_restore(flags);
            return (done > 0) ? (long)done : -EPIPE;
        }
        size_t n = pipe_core_write(&p->core, src + done, len - done);
        done += n;
        if (n > 0) {
            wq_wake_one(&p->read_wait_head, &p->read_wait_tail);
        }
        if (done == len) {
            cpu_irq_restore(flags);
            return (long)done;
        }
        wq_push(&p->write_wait_head, &p->write_wait_tail, thread_current());
        sched_block();
        cpu_irq_restore(flags);
    }
    return (long)done;
}

static long pipe_fstat(struct file *f, struct stat *st) {
    memset(st, 0, sizeof(*st));
    st->st_dev = PIPE_STDEV;
    st->st_ino = (uint64_t)(uintptr_t)f->priv;
    st->st_nlink = 1;
    st->st_mode = S_IFIFO | 0600u;
    st->st_blksize = PIPE_CAPACITY;
    return 0;
}

static void pipe_release_read(struct file *f) {
    struct pipe *p = f->priv;
    uint64_t flags = cpu_irq_save();
    KASSERT(p->readers > 0);
    p->readers--;
    if (p->readers == 0) {
        /* Blocked writers must re-check: no readers left is -EPIPE. */
        wq_wake_all(&p->write_wait_head, &p->write_wait_tail);
    }
    int gone = (p->readers == 0 && p->writers == 0);
    cpu_irq_restore(flags);
    if (gone) {
        kfree(p);
    }
}

static void pipe_release_write(struct file *f) {
    struct pipe *p = f->priv;
    uint64_t flags = cpu_irq_save();
    KASSERT(p->writers > 0);
    p->writers--;
    if (p->writers == 0) {
        /* Blocked readers must re-check: no writers left is EOF. */
        wq_wake_all(&p->read_wait_head, &p->read_wait_tail);
    }
    int gone = (p->readers == 0 && p->writers == 0);
    cpu_irq_restore(flags);
    if (gone) {
        kfree(p);
    }
}

static const struct file_ops PIPE_READ_OPS = {
    .read = pipe_read,
    .write = NULL, /* -EBADF */
    .lseek = NULL, /* -ESPIPE */
    .fstat = pipe_fstat,
    .getdents = NULL, /* -ENOTDIR */
    .fsync = NULL,    /* -EINVAL */
    .release = pipe_release_read,
};

static const struct file_ops PIPE_WRITE_OPS = {
    .read = NULL, /* -EINVAL */
    .write = pipe_write,
    .lseek = NULL,
    .fstat = pipe_fstat,
    .getdents = NULL,
    .fsync = NULL,
    .release = pipe_release_write,
};

long pipe_open(struct file **read_out, struct file **write_out) {
    struct pipe *p = kzalloc(sizeof(*p));
    if (p == NULL) {
        return -ENOMEM;
    }
    pipe_core_init(&p->core, p->buf, sizeof(p->buf));
    p->readers = 1;
    p->writers = 1;

    struct file *rf = kzalloc(sizeof(*rf));
    struct file *wf = kzalloc(sizeof(*wf));
    if (rf == NULL || wf == NULL) {
        kfree(rf);
        kfree(wf);
        kfree(p);
        return -ENOMEM;
    }
    rf->ops = &PIPE_READ_OPS;
    rf->priv = p;
    rf->flags = O_RDONLY;
    rf->refs = 1;
    wf->ops = &PIPE_WRITE_OPS;
    wf->priv = p;
    wf->flags = O_WRONLY;
    wf->refs = 1;

    *read_out = rf;
    *write_out = wf;
    return 0;
}
