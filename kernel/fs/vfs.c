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
 * The root is mounted writable (slice 5d). `node_opens[]` counts the
 * open descriptions per graphfs node, maintained under fs_lock from
 * open to last close, so removal of an open object can answer -EBUSY
 * (the pinned v1 policy — see vfs.h) instead of freeing a node an fd
 * still points at.
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

/* Open-description count per graphfs node id (fs_lock-guarded).
 * Sized node_count entries at mount. */
static uint32_t *node_opens;

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
    case GFS_EEXIST:
        return -EEXIST;
    case GFS_ENOTEMPTY:
        return -ENOTEMPTY;
    case GFS_ENOSPC:
    case GFS_EFRAG:
        /* Out of blocks, node slots, or inline extents: however the
         * space ran out, the write does not fit. */
        return -ENOSPC;
    case GFS_EFBIG:
        return -EFBIG;
    case GFS_EMANYPARENTS:
        /* A second NAME edge onto a directory — Linux's answer to
         * link(2) on a directory. */
        return -EPERM;
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

static int file_is_gfs(const struct file *f);

void vfs_file_put(struct file *f) {
    uint64_t flags = cpu_irq_save();
    KASSERT(f->refs > 0);
    int gone = (--f->refs == 0);
    cpu_irq_restore(flags);
    if (gone) {
        if (file_is_gfs(f)) {
            /* The last reference is gone: the node is no longer held
             * open, so unlink may free it again. */
            mutex_lock(&fs_lock);
            KASSERT(node_opens[f->node] > 0);
            node_opens[f->node]--;
            mutex_unlock(&fs_lock);
        }
        if (f->ops->release != NULL) {
            f->ops->release(f);
        }
        kfree(f);
    }
}

/* ---- graphfs-backed files ---- */

static long gfsf_read(struct file *f, void *buf, size_t len) {
    if ((f->flags & O_ACCMODE) == O_WRONLY) {
        return -EBADF; /* this description was not opened for reading */
    }
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

static long gfsf_write(struct file *f, const void *buf, size_t len) {
    if ((f->flags & O_ACCMODE) == O_RDONLY) {
        return -EBADF; /* this description was not opened for writing */
    }
    mutex_lock(&fs_lock);
    uint64_t off = f->off;
    if ((f->flags & O_APPEND) != 0) {
        /* Every append lands at the current EOF; atomic because the
         * size read and the write share one fs_lock hold. */
        struct gfs_node node;
        int rc = gfs_node_get(root_fs, f->node, &node);
        if (rc != GFS_OK) {
            mutex_unlock(&fs_lock);
            return gfs_errno(rc);
        }
        off = node.size;
    }
    long n = gfs_write(root_fs, f->node, off, buf, len);
    if (n < 0) {
        mutex_unlock(&fs_lock);
        return gfs_errno((int)n);
    }
    f->off = off + (uint64_t)n;
    mutex_unlock(&fs_lock);
    return n;
}

/* Every mutating graphfs call commits before it returns (CoW publish
 * through the write-through block cache), so an fsync has nothing
 * left to flush; it exists so callers can *ask* for durability. */
static long gfsf_fsync(struct file *f) {
    (void)f;
    return 0;
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
    .write = gfsf_write, /* refuses O_RDONLY descriptions itself */
    .lseek = gfsf_lseek,
    .fstat = gfsf_fstat,
    .getdents = NULL, /* -ENOTDIR */
    .fsync = gfsf_fsync,
};

static const struct file_ops GFS_DIR_OPS = {
    .read = gfsf_dir_read,
    .write = NULL, /* -EBADF: directories never open for writing */
    .lseek = gfsf_lseek,
    .fstat = gfsf_fstat,
    .getdents = gfsf_getdents,
    .fsync = gfsf_fsync, /* fsync on a directory fd is valid POSIX */
};

static int file_is_gfs(const struct file *f) {
    return f->ops == &GFS_FILE_OPS || f->ops == &GFS_DIR_OPS;
}

/* ---- resolution and open ---- */

/*
 * Split a canonical path (not "/") into its parent directory path and
 * final component. `parent` must hold VFS_PATH_MAX bytes; `*name`
 * points into `canon`.
 */
static void path_split(const char *canon, char *parent, const char **name) {
    size_t last = 0;
    for (size_t i = 0; canon[i] != '\0'; i++) {
        if (canon[i] == '/') {
            last = i;
        }
    }
    if (last == 0) {
        parent[0] = '/';
        parent[1] = '\0';
    } else {
        memcpy(parent, canon, last);
        parent[last] = '\0';
    }
    *name = canon + last + 1;
}

static long gfs_open(const char *canon, int flags, unsigned mode, struct file **out) {
    int accmode = flags & O_ACCMODE;
    mutex_lock(&fs_lock);
    uint64_t id = 0;
    struct gfs_node node;
    int rc = gfs_resolve(root_fs, canon, &id);
    if (rc == GFS_OK) {
        if ((flags & O_CREAT) != 0 && (flags & O_EXCL) != 0) {
            mutex_unlock(&fs_lock);
            return -EEXIST;
        }
        rc = gfs_node_get(root_fs, id, &node);
    } else if (rc == GFS_ENOENT && (flags & O_CREAT) != 0 && strcmp(canon, "/") != 0) {
        /* Create the missing final component in its parent — under the
         * same lock hold as the failed resolve, so no one races us. */
        char parent[VFS_PATH_MAX];
        const char *name = NULL;
        path_split(canon, parent, &name);
        uint64_t dir = 0;
        rc = gfs_resolve(root_fs, parent, &dir);
        if (rc == GFS_OK) {
            rc = gfs_create_at(root_fs, dir, name, GFS_NODE_DATA, mode & 07777u, &id);
        }
        if (rc == GFS_OK) {
            rc = gfs_node_get(root_fs, id, &node);
        }
    }
    if (rc != GFS_OK) {
        mutex_unlock(&fs_lock);
        return gfs_errno(rc);
    }

    if (node.type == GFS_NODE_NAMESPACE) {
        /* Directories only open read-only, and never for creation or
         * truncation (Linux: EISDIR for all three). */
        if (accmode != O_RDONLY || (flags & (O_CREAT | O_TRUNC)) != 0) {
            mutex_unlock(&fs_lock);
            return -EISDIR;
        }
    } else {
        if ((flags & O_DIRECTORY) != 0) {
            mutex_unlock(&fs_lock);
            return -ENOTDIR;
        }
        if ((flags & O_TRUNC) != 0 && node.size > 0) {
            /* Linux honors O_TRUNC regardless of the access mode. */
            rc = gfs_truncate(root_fs, id, 0);
            if (rc != GFS_OK) {
                mutex_unlock(&fs_lock);
                return gfs_errno(rc);
            }
        }
    }
    const struct file_ops *ops = (node.type == GFS_NODE_NAMESPACE) ? &GFS_DIR_OPS : &GFS_FILE_OPS;
    struct file *f = file_alloc(ops, id, flags);
    if (f == NULL) {
        mutex_unlock(&fs_lock);
        return -ENOMEM;
    }
    node_opens[id]++;
    mutex_unlock(&fs_lock);
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

long vfs_open(const char *path, int flags, unsigned mode, struct file **out) {
    if ((flags & ~(O_ACCMODE | O_CREAT | O_EXCL | O_TRUNC | O_APPEND | O_DIRECTORY)) != 0) {
        return -EINVAL; /* unimplemented flags are refused, not ignored */
    }
    if ((flags & O_DIRECTORY) != 0 && (flags & O_CREAT) != 0) {
        return -EINVAL; /* as Linux: O_DIRECTORY cannot create */
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
    if (root_fs == NULL) {
        return -ENODEV; /* no root filesystem mounted */
    }
    return gfs_open(canon, flags, mode, out);
}

/* ---- namespace mutation ---- */

/* Nonzero when the canonical path belongs to the devfs mount. */
static int on_devfs(const char *canon) {
    return strcmp(canon, "/dev") == 0 || strncmp(canon, "/dev/", 5) == 0;
}

/* Resolve the parent directory of `canon` and look up its final
 * component, under fs_lock (held by the caller). On success *dir_out
 * is the parent's node id, *id_out and *node_out describe the target, and
 * *name_out points at the final component inside `canon`. */
static int gfs_locate(const char *canon, char *parentbuf, uint64_t *dir_out, const char **name_out,
                      uint64_t *id_out, struct gfs_node *node_out) {
    path_split(canon, parentbuf, name_out);
    int rc = gfs_resolve(root_fs, parentbuf, dir_out);
    if (rc != GFS_OK) {
        return rc;
    }
    rc = gfs_lookup(root_fs, *dir_out, *name_out, id_out);
    if (rc != GFS_OK) {
        return rc;
    }
    return gfs_node_get(root_fs, *id_out, node_out);
}

long vfs_mkdir(const char *path, unsigned mode) {
    char canon[VFS_PATH_MAX];
    long rc = vfs_path_norm(path, canon, sizeof(canon));
    if (rc != 0) {
        return rc;
    }
    if (strcmp(canon, "/") == 0 || strcmp(canon, "/dev") == 0) {
        return -EEXIST;
    }
    if (on_devfs(canon)) {
        return -EPERM; /* devfs is structurally immutable */
    }
    if (root_fs == NULL) {
        return -ENODEV;
    }
    char parent[VFS_PATH_MAX];
    const char *name = NULL;
    path_split(canon, parent, &name);
    mutex_lock(&fs_lock);
    uint64_t dir = 0;
    int grc = gfs_resolve(root_fs, parent, &dir);
    if (grc == GFS_OK) {
        uint64_t id = 0;
        grc = gfs_create_at(root_fs, dir, name, GFS_NODE_NAMESPACE, mode & 07777u, &id);
    }
    mutex_unlock(&fs_lock);
    return gfs_errno(grc);
}

long vfs_unlink(const char *path) {
    char canon[VFS_PATH_MAX];
    long rc = vfs_path_norm(path, canon, sizeof(canon));
    if (rc != 0) {
        return rc;
    }
    if (strcmp(canon, "/") == 0 || strcmp(canon, "/dev") == 0) {
        return -EISDIR; /* unlink(2) never removes a directory */
    }
    if (on_devfs(canon)) {
        return -EPERM;
    }
    if (root_fs == NULL) {
        return -ENODEV;
    }
    char parent[VFS_PATH_MAX];
    const char *name = NULL;
    uint64_t dir = 0;
    uint64_t id = 0;
    struct gfs_node node;
    mutex_lock(&fs_lock);
    int grc = gfs_locate(canon, parent, &dir, &name, &id, &node);
    if (grc != GFS_OK) {
        mutex_unlock(&fs_lock);
        return gfs_errno(grc);
    }
    if (node.type == GFS_NODE_NAMESPACE) {
        mutex_unlock(&fs_lock);
        return -EISDIR;
    }
    if (node.nlink == 1 && node_opens[id] > 0) {
        /* Removing the last name of an open file would free a node
         * an fd still references — the pinned v1 -EBUSY policy. */
        mutex_unlock(&fs_lock);
        return -EBUSY;
    }
    grc = gfs_unlink(root_fs, dir, name);
    mutex_unlock(&fs_lock);
    return gfs_errno(grc);
}

long vfs_rmdir(const char *path) {
    char canon[VFS_PATH_MAX];
    long rc = vfs_path_norm(path, canon, sizeof(canon));
    if (rc != 0) {
        return rc;
    }
    if (strcmp(canon, "/") == 0 || strcmp(canon, "/dev") == 0) {
        return -EBUSY; /* the root and mount points are always busy */
    }
    if (on_devfs(canon)) {
        return -EPERM;
    }
    if (root_fs == NULL) {
        return -ENODEV;
    }
    char parent[VFS_PATH_MAX];
    const char *name = NULL;
    uint64_t dir = 0;
    uint64_t id = 0;
    struct gfs_node node;
    mutex_lock(&fs_lock);
    int grc = gfs_locate(canon, parent, &dir, &name, &id, &node);
    if (grc != GFS_OK) {
        mutex_unlock(&fs_lock);
        return gfs_errno(grc);
    }
    if (node.type != GFS_NODE_NAMESPACE) {
        mutex_unlock(&fs_lock);
        return -ENOTDIR;
    }
    if (node_opens[id] > 0) {
        mutex_unlock(&fs_lock);
        return -EBUSY;
    }
    grc = gfs_unlink(root_fs, dir, name); /* core enforces ENOTEMPTY */
    mutex_unlock(&fs_lock);
    return gfs_errno(grc);
}

long vfs_link(const char *oldpath, const char *newpath) {
    char ocanon[VFS_PATH_MAX];
    char ncanon[VFS_PATH_MAX];
    long rc = vfs_path_norm(oldpath, ocanon, sizeof(ocanon));
    if (rc == 0) {
        rc = vfs_path_norm(newpath, ncanon, sizeof(ncanon));
    }
    if (rc != 0) {
        return rc;
    }
    if (on_devfs(ocanon) || on_devfs(ncanon)) {
        /* Both on devfs: it cannot grow names. Across the seam: hard
         * links never span filesystems. */
        return (on_devfs(ocanon) && on_devfs(ncanon)) ? -EPERM : -EXDEV;
    }
    if (strcmp(ncanon, "/") == 0) {
        return -EEXIST;
    }
    if (root_fs == NULL) {
        return -ENODEV;
    }
    char parent[VFS_PATH_MAX];
    const char *name = NULL;
    path_split(ncanon, parent, &name);
    mutex_lock(&fs_lock);
    uint64_t id = 0;
    struct gfs_node node;
    int grc = gfs_resolve(root_fs, ocanon, &id);
    if (grc == GFS_OK) {
        grc = gfs_node_get(root_fs, id, &node);
    }
    if (grc != GFS_OK) {
        mutex_unlock(&fs_lock);
        return gfs_errno(grc);
    }
    if (node.type == GFS_NODE_NAMESPACE) {
        mutex_unlock(&fs_lock);
        return -EPERM; /* link(2) on a directory, as Linux */
    }
    uint64_t dir = 0;
    grc = gfs_resolve(root_fs, parent, &dir);
    if (grc == GFS_OK) {
        grc = gfs_link(root_fs, dir, name, id, GFS_EDGE_NAME);
    }
    mutex_unlock(&fs_lock);
    return gfs_errno(grc);
}

long vfs_rename(const char *oldpath, const char *newpath) {
    char ocanon[VFS_PATH_MAX];
    char ncanon[VFS_PATH_MAX];
    long rc = vfs_path_norm(oldpath, ocanon, sizeof(ocanon));
    if (rc == 0) {
        rc = vfs_path_norm(newpath, ncanon, sizeof(ncanon));
    }
    if (rc != 0) {
        return rc;
    }
    if (strcmp(ocanon, "/") == 0 || strcmp(ocanon, "/dev") == 0 || strcmp(ncanon, "/") == 0 ||
        strcmp(ncanon, "/dev") == 0) {
        return -EBUSY;
    }
    if (on_devfs(ocanon) || on_devfs(ncanon)) {
        return (on_devfs(ocanon) && on_devfs(ncanon)) ? -EPERM : -EXDEV;
    }
    if (root_fs == NULL) {
        return -ENODEV;
    }
    char oparent[VFS_PATH_MAX];
    char nparent[VFS_PATH_MAX];
    const char *oname = NULL;
    const char *nname = NULL;
    path_split(ocanon, oparent, &oname);
    path_split(ncanon, nparent, &nname);
    mutex_lock(&fs_lock);
    uint64_t odir = 0;
    uint64_t ndir = 0;
    int grc = gfs_resolve(root_fs, oparent, &odir);
    if (grc == GFS_OK) {
        grc = gfs_resolve(root_fs, nparent, &ndir);
    }
    if (grc != GFS_OK) {
        mutex_unlock(&fs_lock);
        return gfs_errno(grc);
    }
    /* Replacing an open object would free a node an fd references —
     * unless source and target are the same node (then rename is a
     * no-op and nothing is freed). */
    uint64_t src = 0;
    uint64_t dst = 0;
    if (gfs_lookup(root_fs, odir, oname, &src) == GFS_OK &&
        gfs_lookup(root_fs, ndir, nname, &dst) == GFS_OK && dst != src) {
        struct gfs_node dnode;
        if (gfs_node_get(root_fs, dst, &dnode) == GFS_OK &&
            (dnode.type == GFS_NODE_NAMESPACE || dnode.nlink == 1) && node_opens[dst] > 0) {
            mutex_unlock(&fs_lock);
            return -EBUSY;
        }
    }
    grc = gfs_rename(root_fs, odir, oname, ndir, nname);
    mutex_unlock(&fs_lock);
    return gfs_errno(grc);
}

long vfs_read_file(const char *path, void **buf_out, uint64_t *size_out) {
    char canon[VFS_PATH_MAX];
    long rc = vfs_path_norm(path, canon, sizeof(canon));
    if (rc != 0) {
        return rc;
    }
    if (root_fs == NULL) {
        return -ENODEV;
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

void vfs_stats(uint64_t *generation, uint64_t *free_blocks, uint64_t *free_nodes) {
    mutex_lock(&fs_lock);
    *generation = root_fs->generation;
    *free_blocks = root_fs->free_blocks;
    *free_nodes = root_fs->free_nodes;
    mutex_unlock(&fs_lock);
}

int vfs_has_root(void) {
    return root_fs != NULL;
}

void vfs_init(void) {
    struct bdev *bd = block_root();
    if (bd == NULL) {
        /* No disk driver claimed a device (real hardware without an
         * AHCI/NVMe driver yet, or a deliberately disk-less smoke
         * test). devfs does not need a disk, so /dev/console still
         * works: bring it up and let the caller decide what a system
         * with no root filesystem does, instead of taking the whole
         * boot down with it. */
        kprintf("vfs: no block device found — booting without a root filesystem\n");
        devfs_init();
        kprintf("vfs: devfs at /dev (console)\n");
        return;
    }
    root_fs = kzalloc(sizeof(*root_fs));
    if (root_fs == NULL) {
        panic("vfs: out of memory for the root mount");
    }
    /* Writable mount: the CoW allocator needs its two bitmap copies. */
    size_t work_len = gfs_mount_work_size(bd->nblocks);
    void *work = kmalloc(work_len);
    if (work == NULL) {
        panic("vfs: out of memory for the allocator bitmaps");
    }
    int rc = gfs_mount(root_fs, &GDEV_OPS, bd, 1, work, work_len);
    if (rc != GFS_OK) {
        panic("vfs: mounting graphfs on %s: %s", bd->name, gfs_strerror(rc));
    }
    node_opens = kzalloc((size_t)root_fs->node_count * sizeof(node_opens[0]));
    if (node_opens == NULL) {
        panic("vfs: out of memory for the open-node table");
    }
    kprintf("vfs: graphfs root mounted rw (gen %llu, %llu/%llu blocks free, %llu/%llu nodes "
            "free)\n",
            (unsigned long long)root_fs->generation, (unsigned long long)root_fs->free_blocks,
            (unsigned long long)root_fs->total_blocks, (unsigned long long)root_fs->free_nodes,
            (unsigned long long)root_fs->node_count);

    devfs_init();
    kprintf("vfs: devfs at /dev (console)\n");
}
