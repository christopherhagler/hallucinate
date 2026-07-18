/*
 * devfs.c - synthetic device files under /dev.
 *
 * v1 has one device: /dev/console, the kernel console (serial + VGA)
 * for output and the PS/2 keyboard for input. Reads block until at
 * least one character is available, then return what the buffer
 * holds (terminal-style short reads); the lost-wakeup race is closed
 * the same way wait4 closes it — publish the waiting thread and
 * block inside one interrupts-off section, with the keyboard IRQ as
 * the waker (via keyboard_set_notify). `read_lock` serializes
 * readers so the single waiter slot suffices.
 *
 * devfs is not on disk: the node ids here are devfs-local (1 = the
 * /dev directory, 2 = console), reported with st_dev 2 so they never
 * collide with graphfs inos. The /dev directory itself exists on the
 * graphfs image as an empty namespace node (a conventional mount
 * point); resolution never reaches it because the VFS mount table
 * matches first.
 */
#include <vfs.h>

#include <stddef.h>
#include <stdint.h>

#include <arch/x86_64/cpu.h>
#include <console.h>
#include <errno.h>
#include <keyboard.h>
#include <kmalloc.h>
#include <mutex.h>
#include <panic.h>
#include <sched.h>
#include <string.h>

#define DEVFS_STDEV   2u
#define DEVFS_INO_DIR 1u
#define DEVFS_INO_CON 2u

/* Linux gives the system console character device 5:1. */
#define CONSOLE_RDEV ((5u << 8) | 1u)

static struct mutex read_lock = MUTEX_INITIALIZER("console-read");
static struct thread *reader; /* thread blocked waiting for input */

/* Keyboard IRQ context, interrupts off: wake a blocked reader. */
static void kbd_input_notify(void) {
    if (reader != NULL) {
        struct thread *t = reader;
        reader = NULL;
        sched_wake(t);
    }
}

static long con_read(struct file *f, void *buf, size_t len) {
    (void)f;
    if (len == 0) {
        return 0;
    }
    char *dst = buf;
    size_t n = 0;
    mutex_lock(&read_lock);
    while (n < len) {
        uint64_t flags = cpu_irq_save();
        int c = keyboard_getchar();
        if (c < 0) {
            if (n > 0) {
                cpu_irq_restore(flags);
                break; /* got something: short read, don't wait */
            }
            KASSERT(reader == NULL);
            reader = thread_current();
            sched_block();
            cpu_irq_restore(flags);
            continue;
        }
        cpu_irq_restore(flags);
        dst[n++] = (char)c;
    }
    mutex_unlock(&read_lock);
    return (long)n;
}

static long con_write(struct file *f, const void *buf, size_t len) {
    (void)f;
    console_write(buf, len);
    return (long)len;
}

static long con_fstat(struct file *f, struct stat *st) {
    (void)f;
    memset(st, 0, sizeof(*st));
    st->st_dev = DEVFS_STDEV;
    st->st_ino = DEVFS_INO_CON;
    st->st_nlink = 1;
    st->st_mode = S_IFCHR | 0666u;
    st->st_rdev = CONSOLE_RDEV;
    st->st_blksize = 4096;
    return 0;
}

static const struct file_ops CON_OPS = {
    .read = con_read,
    .write = con_write,
    .lseek = NULL, /* -ESPIPE */
    .fstat = con_fstat,
    .getdents = NULL,
};

/* ---- the /dev directory itself ---- */

static long dir_read(struct file *f, void *buf, size_t len) {
    (void)f;
    (void)buf;
    (void)len;
    return -EISDIR;
}

static long dir_lseek(struct file *f, int64_t off, int whence) {
    int64_t base;
    switch (whence) {
    case SEEK_SET:
        base = 0;
        break;
    case SEEK_CUR:
        base = (int64_t)f->off;
        break;
    default:
        return -EINVAL;
    }
    int64_t pos = base + off;
    if (pos < 0) {
        return -EINVAL;
    }
    f->off = (uint64_t)pos;
    return pos;
}

static long dir_fstat(struct file *f, struct stat *st) {
    (void)f;
    memset(st, 0, sizeof(*st));
    st->st_dev = DEVFS_STDEV;
    st->st_ino = DEVFS_INO_DIR;
    st->st_nlink = 2;
    st->st_mode = S_IFDIR | 0755u;
    st->st_blksize = 4096;
    return 0;
}

/* One record per cursor position: ".", "..", then the devices. The
 * dirent encoder is duplicated from vfs.c knowingly — three static
 * entries do not justify exporting it. */
static long dir_getdents(struct file *f, void *buf, size_t len) {
    static const struct {
        const char *name;
        uint64_t ino;
        uint8_t type;
    } ents[] = {
        {".", DEVFS_INO_DIR, DT_DIR},
        {"..", 1, DT_DIR}, /* the graphfs root */
        {"console", DEVFS_INO_CON, DT_CHR},
    };
    size_t out = 0;
    while (f->off < sizeof(ents) / sizeof(ents[0])) {
        const char *name = ents[f->off].name;
        size_t nlen = strlen(name);
        size_t reclen = (DIRENT64_HDR + nlen + 1 + 7) & ~(size_t)7;
        if (out + reclen > len) {
            return (out == 0) ? -EINVAL : (long)out;
        }
        _Alignas(8) uint8_t rec[DIRENT64_HDR + 16];
        struct dirent64 *d = (struct dirent64 *)rec;
        d->d_ino = ents[f->off].ino;
        d->d_off = (int64_t)(f->off + 1);
        d->d_reclen = (uint16_t)reclen;
        d->d_type = ents[f->off].type;
        memcpy(rec + DIRENT64_HDR, name, nlen + 1);
        memset(rec + DIRENT64_HDR + nlen + 1, 0, reclen - DIRENT64_HDR - nlen - 1);
        memcpy((uint8_t *)buf + out, rec, reclen);
        out += reclen;
        f->off++;
    }
    return (long)out;
}

static const struct file_ops DIR_OPS = {
    .read = dir_read,
    .write = NULL,
    .lseek = dir_lseek,
    .fstat = dir_fstat,
    .getdents = dir_getdents,
};

long devfs_open(const char *rel, int flags, struct file **out) {
    const struct file_ops *ops;
    uint64_t node;
    if (rel[0] == '\0') {
        if ((flags & O_ACCMODE) != O_RDONLY || (flags & (O_CREAT | O_TRUNC)) != 0) {
            return -EISDIR;
        }
        ops = &DIR_OPS;
        node = DEVFS_INO_DIR;
    } else if (strcmp(rel, "console") == 0) {
        if ((flags & O_DIRECTORY) != 0) {
            return -ENOTDIR;
        }
        if ((flags & O_CREAT) != 0 && (flags & O_EXCL) != 0) {
            return -EEXIST;
        }
        /* O_TRUNC on a character device has no effect (as Linux). */
        ops = &CON_OPS;
        node = DEVFS_INO_CON;
    } else if (strncmp(rel, "console/", 8) == 0) {
        /* A path *through* a known device is ENOTDIR, not ENOENT. */
        return -ENOTDIR;
    } else {
        /* Missing name directly under /dev: creation is refused —
         * devfs cannot grow nodes. A missing *intermediate* directory
         * is plain ENOENT even with O_CREAT, as everywhere else. */
        int nested = 0;
        for (const char *p = rel; *p != '\0'; p++) {
            if (*p == '/') {
                nested = 1;
                break;
            }
        }
        return (!nested && (flags & O_CREAT) != 0) ? -EPERM : -ENOENT;
    }
    struct file *f = kzalloc(sizeof(*f));
    if (f == NULL) {
        return -ENOMEM;
    }
    f->ops = ops;
    f->node = node;
    f->flags = flags;
    f->refs = 1;
    *out = f;
    return 0;
}

void devfs_init(void) {
    keyboard_set_notify(kbd_input_notify);
}
