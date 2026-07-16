/*
 * vfs.c - mounts, path resolution, open files, and the graphfs vnode ops.
 *
 * Layering (top to bottom): syscalls validate user pointers and call
 * vfs_* / file ops; this file resolves canonical paths through the
 * mount table and implements the graphfs-backed file operations over
 * gfs_* core calls; the core reads through the block cache and the
 * virtio driver.
 *
 * Locking: `fs_lock` (sleeping mutex) serializes every gfs_* call —
 * the struct gfs scratch buffers are single-caller by contract — and
 * every graphfs file-offset update, discharging the block layer's
 * "one caller at a time" rule for all disk paths. It is taken for
 * the duration of one operation, never across a return to userspace.
 *
 * The mount is read-only in slice 5c: gfs write ops are never
 * reached (open refuses O_WRONLY/O_RDWR with -EROFS). Slice 5d
 * remounts writable and adds the write-side syscalls.
 */
#include <vfs.h>

#include <stddef.h>
#include <stdint.h>

#include <arch/x86_64/cpu.h>
#include <block.h>
#include <errno.h>
#include <graphfs_core.h>
#include <kmalloc.h>
#include <kprintf.h>
#include <mutex.h>
#include <panic.h>
#include <string.h>

/* The Linux x86_64 stat layout this kernel promises (docs/book/appendix-h-userspace.md). */
_Static_assert(sizeof(struct stat) == 144, "Linux x86_64 struct stat is 144 bytes");

/* st_dev values, per mount. */
#define VFS_DEV_GFS   1u
#define VFS_DEV_DEVFS 2u

static struct gfs *root_fs;
static struct mutex fs_lock = MUTEX_INITIALIZER("vfs");

/* ---- block-device callbacks for the graphfs core ---- */

static int gdev_read(void *ctx, uint64_t lba, void *buf) {
    return block_read(ctx, lba, buf);
}

static int gdev_write(void *ctx, uint64_t lba, const void *buf) {
    return block_write(ctx, lba, buf);
}

static const struct gfs_ops GDEV_OPS = {gdev_read, gdev_write};

/* Map a gfs_err to the errno this kernel returns for it. */
static long gfs_errno(int rc) {
    switch (rc) {
    case GFS_OK:
        return 0;
    case GFS_ENOENT:
        return -ENOENT;
    case GFS_ENOTDIR:
        return -ENOTDIR;
    case GFS_EISDIR:
        return -EISDIR;
    case GFS_ENAMETOOLONG:
        return -ENAMETOOLONG;
    case GFS_EROFS:
        return -EROFS;
    case GFS_EINVAL:
        return -EINVAL;
    default:
        /* EIO covers device failure and detected corruption
         * (GFS_EBADCRC/GFS_ECORRUPT): the data cannot be served. */
        return -EIO;
    }
}

/* ---- open file descriptions ---- */

static struct file *file_alloc(const struct file_ops *ops, uint64_t node, int flags) {
    struct file *f = kzalloc(sizeof(*f));
    if (f == NULL) {
        return NULL;
    }
    f->ops = ops;
    f->node = node;
    f->flags = flags;
    f->refs = 1;
    return f;
}

void vfs_file_ref(struct file *f) {
    uint64_t flags = cpu_irq_save();
    KASSERT(f->refs > 0);
    f->refs++;
    cpu_irq_restore(flags);
}

void vfs_file_put(struct file *f) {
    uint64_t flags = cpu_irq_save();
    KASSERT(f->refs > 0);
    int gone = (--f->refs == 0);
    cpu_irq_restore(flags);
    if (gone) {
        kfree(f);
    }
}

/* ---- graphfs-backed files ---- */

static long gfsf_read(struct file *f, void *buf, size_t len) {
    mutex_lock(&fs_lock);
    long n = gfs_read(root_fs, f->node, f->off, buf, len);
    if (n < 0) {
        mutex_unlock(&fs_lock);
        return gfs_errno((int)n);
    }
    f->off += (uint64_t)n;
    mutex_unlock(&fs_lock);
    return n;
}

static long gfsf_lseek(struct file *f, int64_t off, int whence) {
    mutex_lock(&fs_lock);
    int64_t base;
    switch (whence) {
    case SEEK_SET:
        base = 0;
        break;
    case SEEK_CUR:
        base = (int64_t)f->off;
        break;
    case SEEK_END: {
        struct gfs_node node;
        int rc = gfs_node_get(root_fs, f->node, &node);
        if (rc != GFS_OK) {
            mutex_unlock(&fs_lock);
            return gfs_errno(rc);
        }
        base = (int64_t)node.size;
        break;
    }
    default:
        mutex_unlock(&fs_lock);
        return -EINVAL;
    }
    int64_t pos = base + off;
    if (pos < 0) {
        mutex_unlock(&fs_lock);
        return -EINVAL;
    }
    f->off = (uint64_t)pos;
    mutex_unlock(&fs_lock);
    return pos;
}

static long gfsf_fstat(struct file *f, struct stat *st) {
    mutex_lock(&fs_lock);
    struct gfs_node node;
    int rc = gfs_node_get(root_fs, f->node, &node);
    mutex_unlock(&fs_lock);
    if (rc != GFS_OK) {
        return gfs_errno(rc);
    }
    memset(st, 0, sizeof(*st));
    st->st_dev = VFS_DEV_GFS;
    st->st_ino = f->node;
    st->st_nlink = node.nlink;
    st->st_mode = (node.type == GFS_NODE_NAMESPACE ? S_IFDIR : S_IFREG) | (node.mode & 07777u);
    st->st_size = (int64_t)node.size;
    st->st_blksize = GFS_BLOCK_SIZE;
    st->st_blocks = (int64_t)((node.size + 511) / 512);
    return 0;
}

/* read(2) on a directory: the namespace is enumerated with
 * getdents64, never byte reads. */
static long gfsf_dir_read(struct file *f, void *buf, size_t len) {
    (void)f;
    (void)buf;
    (void)len;
    return -EISDIR;
}

/* Append one dirent64 to buf[out..len); 0 = no room. */
static size_t dirent_emit(void *buf, size_t out, size_t len, uint64_t ino, int64_t next_off,
                          uint8_t type, const char *name) {
    size_t nlen = strlen(name);
    size_t reclen = (DIRENT64_HDR + nlen + 1 + 7) & ~(size_t)7;
    if (out + reclen > len) {
        return 0;
    }
    _Alignas(8) uint8_t rec[DIRENT64_HDR + VFS_NAME_MAX + 1 + 7];
    struct dirent64 *d = (struct dirent64 *)rec;
    d->d_ino = ino;
    d->d_off = next_off;
    d->d_reclen = (uint16_t)reclen;
    d->d_type = type;
    memcpy(rec + DIRENT64_HDR, name, nlen + 1);
    memset(rec + DIRENT64_HDR + nlen + 1, 0, reclen - DIRENT64_HDR - nlen - 1);
    memcpy((uint8_t *)buf + out, rec, reclen);
    return reclen;
}

/*
 * getdents64 over a NAMESPACE node. The file offset is a cursor:
 * 0 = ".", 1 = "..", 2+i = outgoing edge i. Non-NAME edges (TAG/REF,
 * the Phase 6 semantic layer) are not namespace entries and are
 * skipped. Returns bytes written, 0 at end, or -EINVAL if the buffer
 * cannot hold even one record (Linux semantics).
 */
static long gfsf_getdents(struct file *f, void *buf, size_t len) {
    mutex_lock(&fs_lock);
    struct gfs_node dir;
    int rc = gfs_node_get(root_fs, f->node, &dir);
    if (rc != GFS_OK) {
        mutex_unlock(&fs_lock);
        return gfs_errno(rc);
    }
    size_t out = 0;
    long err = 0;
    while (err == 0) {
        uint64_t idx = f->off;
        size_t emitted;
        if (idx == 0) {
            emitted = dirent_emit(buf, out, len, f->node, 1, DT_DIR, ".");
        } else if (idx == 1) {
            /* The root is its own parent (gfs stores parent 0). */
            uint64_t up = (dir.parent != 0) ? dir.parent : f->node;
            emitted = dirent_emit(buf, out, len, up, 2, DT_DIR, "..");
        } else if (idx - 2 < dir.edge_count) {
            struct gfs_edge edge;
            rc = gfs_edge_get(root_fs, f->node, (uint32_t)(idx - 2), &edge);
            if (rc != GFS_OK) {
                err = gfs_errno(rc);
                break;
            }
            if (edge.type != GFS_EDGE_NAME) {
                f->off++;
                continue;
            }
            struct gfs_node target;
            rc = gfs_node_get(root_fs, edge.target, &target);
            if (rc != GFS_OK) {
                err = gfs_errno(rc);
                break;
            }
            uint8_t type = (target.type == GFS_NODE_NAMESPACE) ? DT_DIR : DT_REG;
            emitted = dirent_emit(buf, out, len, edge.target, (int64_t)(idx + 1), type, edge.name);
        } else {
            break; /* end of directory */
        }
        if (emitted == 0) {
            if (out == 0) {
                err = -EINVAL; /* buffer too small for one record */
            }
            break;
        }
        out += emitted;
        f->off++;
    }
    mutex_unlock(&fs_lock);
    return (err != 0) ? err : (long)out;
}

static const struct file_ops GFS_FILE_OPS = {
    .read = gfsf_read,
    .write = NULL, /* -EBADF: 5c opens are read-only */
    .lseek = gfsf_lseek,
    .fstat = gfsf_fstat,
    .getdents = NULL, /* -ENOTDIR */
};

static const struct file_ops GFS_DIR_OPS = {
    .read = gfsf_dir_read,
    .write = NULL,
    .lseek = gfsf_lseek,
    .fstat = gfsf_fstat,
    .getdents = gfsf_getdents,
};

/* ---- resolution and open ---- */

static long gfs_open(const char *canon, int flags, struct file **out) {
    mutex_lock(&fs_lock);
    uint64_t id = 0;
    int rc = gfs_resolve(root_fs, canon, &id);
    struct gfs_node node;
    if (rc == GFS_OK) {
        rc = gfs_node_get(root_fs, id, &node);
    }
    mutex_unlock(&fs_lock);
    if (rc != GFS_OK) {
        return gfs_errno(rc);
    }

    int accmode = flags & O_ACCMODE;
    if (node.type == GFS_NODE_NAMESPACE) {
        if (accmode != O_RDONLY) {
            return -EISDIR;
        }
    } else {
        if ((flags & O_DIRECTORY) != 0) {
            return -ENOTDIR;
        }
        if (accmode != O_RDONLY) {
            return -EROFS; /* read-only mount until 5d */
        }
    }
    const struct file_ops *ops = (node.type == GFS_NODE_NAMESPACE) ? &GFS_DIR_OPS : &GFS_FILE_OPS;
    struct file *f = file_alloc(ops, id, flags);
    if (f == NULL) {
        return -ENOMEM;
    }
    *out = f;
    return 0;
}

/* Compile-time mount table: longest-prefix match on the canonical
 * path; anything unmatched belongs to the graphfs root. */
struct mount {
    const char *prefix;
    size_t len;
    long (*open)(const char *rel, int flags, struct file **out);
};

static const struct mount MOUNTS[] = {
    {"/dev", 4, devfs_open},
};

long vfs_open(const char *path, int flags, struct file **out) {
    if ((flags & ~(O_ACCMODE | O_DIRECTORY)) != 0) {
        return -EINVAL; /* no O_CREAT and friends until the write path */
    }
    char canon[VFS_PATH_MAX];
    long rc = vfs_path_norm(path, canon, sizeof(canon));
    if (rc != 0) {
        return rc;
    }
    for (unsigned i = 0; i < sizeof(MOUNTS) / sizeof(MOUNTS[0]); i++) {
        const struct mount *m = &MOUNTS[i];
        if (strncmp(canon, m->prefix, m->len) == 0 &&
            (canon[m->len] == '\0' || canon[m->len] == '/')) {
            const char *rel = canon + m->len;
            while (*rel == '/') {
                rel++;
            }
            return m->open(rel, flags, out);
        }
    }
    return gfs_open(canon, flags, out);
}

long vfs_read_file(const char *path, void **buf_out, uint64_t *size_out) {
    char canon[VFS_PATH_MAX];
    long rc = vfs_path_norm(path, canon, sizeof(canon));
    if (rc != 0) {
        return rc;
    }

    mutex_lock(&fs_lock);
    uint64_t id = 0;
    int grc = gfs_resolve(root_fs, canon, &id);
    struct gfs_node node;
    if (grc == GFS_OK) {
        grc = gfs_node_get(root_fs, id, &node);
    }
    if (grc != GFS_OK) {
        mutex_unlock(&fs_lock);
        return gfs_errno(grc);
    }
    if (node.type != GFS_NODE_DATA) {
        mutex_unlock(&fs_lock);
        return -EISDIR;
    }
    uint8_t *buf = kmalloc(node.size > 0 ? node.size : 1);
    if (buf == NULL) {
        mutex_unlock(&fs_lock);
        return -ENOMEM;
    }
    uint64_t done = 0;
    while (done < node.size) {
        long n = gfs_read(root_fs, id, done, buf + done, node.size - done);
        if (n <= 0) {
            mutex_unlock(&fs_lock);
            kfree(buf);
            return (n < 0) ? gfs_errno((int)n) : -EIO;
        }
        done += (uint64_t)n;
    }
    mutex_unlock(&fs_lock);
    *buf_out = buf;
    *size_out = node.size;
    return 0;
}

void vfs_init(void) {
    struct bdev *bd = block_root();
    if (bd == NULL) {
        panic("vfs: no block device to mount the root filesystem from");
    }
    root_fs = kzalloc(sizeof(*root_fs));
    if (root_fs == NULL) {
        panic("vfs: out of memory for the root mount");
    }
    /* Read-only mount: no allocator bitmaps needed (5d turns on the
     * write path and mounts writable). */
    int rc = gfs_mount(root_fs, &GDEV_OPS, bd, 0, NULL, 0);
    if (rc != GFS_OK) {
        panic("vfs: mounting graphfs on %s: %s", bd->name, gfs_strerror(rc));
    }
    kprintf("vfs: graphfs root mounted ro (gen %llu, %llu/%llu blocks free, %llu/%llu nodes "
            "free)\n",
            (unsigned long long)root_fs->generation, (unsigned long long)root_fs->free_blocks,
            (unsigned long long)root_fs->total_blocks, (unsigned long long)root_fs->free_nodes,
            (unsigned long long)root_fs->node_count);

    devfs_init();
    kprintf("vfs: devfs at /dev (console)\n");
}
