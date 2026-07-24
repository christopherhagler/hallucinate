/*
 * socket.c - local (AF_UNIX) socket pairs: two full-duplex file
 * descriptions over two shared rings, unattached to any mount.
 *
 * socketpair(2) is pipe(2)'s bidirectional sibling, and it is built
 * the same way: each direction is one pipe_core.c ring (pure and
 * already host-tested), one end's write ring is the other end's read
 * ring, and blocking/wakeup follows the identical interrupts-off
 * discipline as kernel/fs/pipe.c (see that file's header comment for
 * the lost-wakeup argument — it applies verbatim here). What's new is
 * that each fd is a single description that is *both* a reader and a
 * writer, so one close() must retire both roles atomically rather
 * than pipe's split "read fd" / "write fd" release.
 *
 * Only AF_UNIX + SOCK_STREAM + protocol 0 is implemented — the only
 * combination any caller in this kernel needs (fork-shared IPC
 * endpoints for the AI daemon and, later, the GUI compositor). Named
 * sockets (bind/listen/accept/connect) are not: nothing yet needs two
 * *unrelated* processes to rendezvous by name, only a parent hand two
 * connected endpoints to children the way pipe(2) does today. Revisit
 * when Phase 8's compositor needs unrelated clients to find it.
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

#define SOCK_CAPACITY 4096u

/* st_dev for local sockets: distinct from graphfs (1), devfs (2), and
 * pipes (3). Like pipes, sockets have no path and no directory entry. */
#define SOCKET_STDEV 5u

/* One direction of the pair: end `writer` (0 or 1) writes here, the
 * other end reads it. */
struct sock_chan {
    struct pipe_core core;
    uint8_t buf[SOCK_CAPACITY];
    struct thread *read_wait_head, *read_wait_tail;
    struct thread *write_wait_head, *write_wait_tail;
};

struct socket_pair {
    struct sock_chan chan[2]; /* chan[i] is written by end i, read by end 1-i */
    int end_open[2];          /* 1 while that end's description is still open */
};

struct socket_end {
    struct socket_pair *pair;
    int self; /* which end (0 or 1) this file object is */
};

/* ---- FIFO wait-queue helpers (thread->next), identical to pipe.c's;
 * not shared with it because the two ring counts they manage (open
 * end vs. reader/writer refcount) mean slightly different recheck
 * conditions, and there's no third user yet to justify factoring a
 * common module. ---- */

static void wq_push(struct thread **head, struct thread **tail, struct thread *t) {
    t->next = NULL;
    if (*tail != NULL) {
        (*tail)->next = t;
    } else {
        *head = t;
    }
    *tail = t;
}

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

static long socket_read(struct file *f, void *buf, size_t len) {
    struct socket_end *e = f->priv;
    struct socket_pair *p = e->pair;
    struct sock_chan *c = &p->chan[1 - e->self]; /* the peer writes here */
    if (len == 0) {
        return 0;
    }
    for (;;) {
        uint64_t flags = cpu_irq_save();
        size_t n = pipe_core_read(&c->core, buf, len);
        if (n > 0) {
            wq_wake_one(&c->write_wait_head, &c->write_wait_tail);
            cpu_irq_restore(flags);
            return (long)n;
        }
        if (!p->end_open[1 - e->self]) {
            cpu_irq_restore(flags); /* no data, peer gone: EOF */
            return 0;
        }
        wq_push(&c->read_wait_head, &c->read_wait_tail, thread_current());
        sched_block();
        cpu_irq_restore(flags);
    }
}

static long socket_write(struct file *f, const void *buf, size_t len) {
    struct socket_end *e = f->priv;
    struct socket_pair *p = e->pair;
    struct sock_chan *c = &p->chan[e->self]; /* this end writes here, peer reads it */
    if (len == 0) {
        return 0;
    }
    const uint8_t *src = buf;
    size_t done = 0;
    while (done < len) {
        uint64_t flags = cpu_irq_save();
        if (!p->end_open[1 - e->self]) {
            cpu_irq_restore(flags);
            return (done > 0) ? (long)done : -EPIPE;
        }
        size_t n = pipe_core_write(&c->core, src + done, len - done);
        done += n;
        if (n > 0) {
            wq_wake_one(&c->read_wait_head, &c->read_wait_tail);
        }
        if (done == len) {
            cpu_irq_restore(flags);
            return (long)done;
        }
        wq_push(&c->write_wait_head, &c->write_wait_tail, thread_current());
        sched_block();
        cpu_irq_restore(flags);
    }
    return (long)done;
}

static long socket_fstat(struct file *f, struct stat *st) {
    memset(st, 0, sizeof(*st));
    st->st_dev = SOCKET_STDEV;
    st->st_ino = (uint64_t)(uintptr_t)((struct socket_end *)f->priv)->pair;
    st->st_nlink = 1;
    st->st_mode = S_IFSOCK | 0600u;
    st->st_blksize = SOCK_CAPACITY;
    return 0;
}

static void socket_release(struct file *f) {
    struct socket_end *e = f->priv;
    struct socket_pair *p = e->pair;
    uint64_t flags = cpu_irq_save();
    KASSERT(p->end_open[e->self]);
    p->end_open[e->self] = 0;
    /* This end was the peer's reader on chan[e->self]: wake its
     * blocked writers so they re-check and see -EPIPE. This end was
     * also the peer's writer on chan[1-e->self]: wake its blocked
     * readers so they re-check and see EOF (once drained). */
    struct sock_chan *mine = &p->chan[e->self];
    struct sock_chan *theirs = &p->chan[1 - e->self];
    wq_wake_all(&mine->write_wait_head, &mine->write_wait_tail);
    wq_wake_all(&theirs->read_wait_head, &theirs->read_wait_tail);
    int gone = !p->end_open[0] && !p->end_open[1];
    cpu_irq_restore(flags);
    kfree(e);
    if (gone) {
        kfree(p);
    }
}

static const struct file_ops SOCKET_OPS = {
    .read = socket_read,
    .write = socket_write,
    .lseek = NULL, /* -ESPIPE */
    .fstat = socket_fstat,
    .getdents = NULL, /* -ENOTDIR */
    .fsync = NULL,    /* -EINVAL */
    .release = socket_release,
};

long socketpair_open(int domain, int type, int protocol, struct file **a_out, struct file **b_out) {
    if (domain != AF_UNIX) {
        return -EAFNOSUPPORT;
    }
    if (type != SOCK_STREAM) {
        return -EPROTONOSUPPORT;
    }
    if (protocol != 0) {
        return -EPROTONOSUPPORT;
    }

    struct socket_pair *p = kzalloc(sizeof(*p));
    if (p == NULL) {
        return -ENOMEM;
    }
    pipe_core_init(&p->chan[0].core, p->chan[0].buf, sizeof(p->chan[0].buf));
    pipe_core_init(&p->chan[1].core, p->chan[1].buf, sizeof(p->chan[1].buf));
    p->end_open[0] = 1;
    p->end_open[1] = 1;

    struct socket_end *ea = kzalloc(sizeof(*ea));
    struct socket_end *eb = kzalloc(sizeof(*eb));
    struct file *fa = kzalloc(sizeof(*fa));
    struct file *fb = kzalloc(sizeof(*fb));
    if (ea == NULL || eb == NULL || fa == NULL || fb == NULL) {
        kfree(ea);
        kfree(eb);
        kfree(fa);
        kfree(fb);
        kfree(p);
        return -ENOMEM;
    }
    ea->pair = p;
    ea->self = 0;
    eb->pair = p;
    eb->self = 1;

    fa->ops = &SOCKET_OPS;
    fa->priv = ea;
    fa->flags = O_RDWR;
    fa->refs = 1;
    fb->ops = &SOCKET_OPS;
    fb->priv = eb;
    fb->flags = O_RDWR;
    fb->refs = 1;

    *a_out = fa;
    *b_out = fb;
    return 0;
}
