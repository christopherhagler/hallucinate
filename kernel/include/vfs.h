/*
 * vfs.h - the virtual filesystem: one namespace over many filesystems.
 *
 * The VFS owns path resolution and the open-file abstraction; the
 * filesystems behind it supply `struct file_ops` vtables. v1 mounts
 * are compile-time: the graphfs on the root block device is `/`, and
 * devfs — synthetic device nodes, `/dev/console` today — covers the
 * `/dev` subtree (the mkfs manifest creates the empty `/dev`
 * directory on disk, like any mount point).
 *
 * Paths are normalized lexically before resolution (vfs_path_norm,
 * pure and host-tested): "." and ".." never reach a filesystem.
 * Every process's working directory is "/" — relative paths resolve
 * from the root until chdir exists.
 *
 * Slice 5c scope: the read side. The graphfs root is mounted
 * read-only; open(O_WRONLY/O_RDWR) on it reports -EROFS. The write
 * path (write/mkdir/link/unlink/rename/fsync) is slice 5d.
 *
 * Concurrency: one global sleeping lock inside vfs.c serializes all
 * graphfs and block-device access (the gfs handle's scratch buffers
 * are single-caller by contract); devfs blocking reads take their
 * own lock. A `struct file` offset is only touched under the lock of
 * the filesystem that owns the file.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include <stat.h>

#define VFS_PATH_MAX 256u /* canonical path, including the NUL */
#define VFS_NAME_MAX 255u /* one component (matches GFS_NAME_MAX) */

struct file;

/*
 * Per-file operations. A NULL slot means the operation does not
 * apply; the syscall layer maps it to the conventional error:
 * read -EINVAL, write -EBADF, lseek -ESPIPE, getdents -ENOTDIR
 * (fstat is mandatory). read/write/getdents return bytes transferred
 * or -errno and advance the file offset themselves; buffers are
 * kernel-valid or caller-validated user ranges.
 */
struct file_ops {
    long (*read)(struct file *f, void *buf, size_t len);
    long (*write)(struct file *f, const void *buf, size_t len);
    long (*lseek)(struct file *f, int64_t off, int whence);
    long (*fstat)(struct file *f, struct stat *st);
    long (*getdents)(struct file *f, void *buf, size_t len);
};

/*
 * An open file description (POSIX term): allocated by open, shared —
 * offset included — by every fd that fork duplicated, freed when the
 * last reference closes. `node` identifies the object inside its
 * filesystem (graphfs node id; devfs minor).
 */
struct file {
    const struct file_ops *ops;
    uint64_t node;
    uint64_t off;
    int flags; /* O_* as given to open */
    int refs;  /* fd-table references, vfs.c-owned */
};

/*
 * Mount the graphfs root (read-only) from the registered block
 * device and initialize devfs. Requires the block device, kmalloc,
 * and the scheduler. Panics if there is no disk or no valid
 * filesystem on it — nothing can run without one.
 */
void vfs_init(void);

/*
 * Resolve `path` and build an open file description. Flags beyond
 * the access mode and O_DIRECTORY are rejected (-EINVAL). Returns 0
 * with *out holding one reference, or -errno.
 */
long vfs_open(const char *path, int flags, struct file **out);

/* Reference counting for fd tables: fork takes, close drops; the
 * last drop frees the description. */
void vfs_file_ref(struct file *f);
void vfs_file_put(struct file *f);

/*
 * Read a whole DATA file into a fresh kmalloc buffer (for execve).
 * On success *buf_out and *size_out describe the image and the caller
 * kfree()s it; empty files yield a 1-byte buffer and size 0.
 */
long vfs_read_file(const char *path, void **buf_out, uint64_t *size_out);

/*
 * Lexical path normalization (pure, host-tested; kernel/fs/vfs_path.c).
 * Writes the canonical absolute form of `in` — no ".", "..", empty
 * components, or trailing slash; ".." above the root stays at the
 * root; relative input is taken from "/". Returns 0, -EINVAL (empty
 * input), or -ENAMETOOLONG (component over VFS_NAME_MAX or result
 * over cap-1 bytes). `cap` is the size of `out`.
 */
long vfs_path_norm(const char *in, char *out, size_t cap);

/* devfs (kernel/fs/devfs.c): called only by vfs.c. `rel` is the
 * canonical path below the /dev mount ("" = the directory itself). */
void devfs_init(void);
long devfs_open(const char *rel, int flags, struct file **out);
