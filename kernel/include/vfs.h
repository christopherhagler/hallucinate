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
 * The graphfs root is mounted writable (slice 5d): open takes
 * O_CREAT/O_EXCL/O_TRUNC/O_APPEND, and mkdir/rmdir/unlink/link/
 * rename mutate the namespace. devfs stays structurally immutable
 * (-EPERM), and mount points ("/", "/dev") refuse removal (-EBUSY).
 *
 * v1 unlink policy, pinned here on purpose: removing the *last* NAME
 * edge of a file some description still has open is -EBUSY. POSIX
 * instead keeps the unlinked file alive until the last close; that
 * needs on-disk orphan tracking (or leaks nodes across a crash), so
 * it is deferred to the Linux personality work (Phase 7). Hard links
 * make the common temp-file shapes expressible meanwhile.
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
 * Per-file operations. A NULL slot means the *object* does not
 * support the operation; the syscall layer maps it to the
 * conventional error: read -EINVAL, write -EBADF, lseek -ESPIPE,
 * getdents -ENOTDIR, fsync -EINVAL (fstat is mandatory). Whether
 * this *description* may read or write (the open's access mode) is
 * enforced inside the ops with -EBADF. read/write/getdents return
 * bytes transferred or -errno and advance the file offset
 * themselves; buffers are kernel-valid or caller-validated user
 * ranges.
 */
struct file_ops {
    long (*read)(struct file *f, void *buf, size_t len);
    long (*write)(struct file *f, const void *buf, size_t len);
    long (*lseek)(struct file *f, int64_t off, int whence);
    long (*fstat)(struct file *f, struct stat *st);
    long (*getdents)(struct file *f, void *buf, size_t len);
    long (*fsync)(struct file *f);
    /* Optional: called once, right before the description is freed
     * (the last reference just dropped). Objects with shared state
     * outside the description itself use this to tear it down — a
     * pipe end decrements its shared buffer's reader/writer count
     * and wakes the other side here. NULL for objects with nothing
     * to do (graphfs and devfs manage their own state elsewhere). */
    void (*release)(struct file *f);
};

/*
 * An open file description (POSIX term): allocated by open, shared —
 * offset included — by every fd that fork duplicated, freed when the
 * last reference closes. `node` identifies the object inside its
 * filesystem (graphfs node id; devfs minor); `priv` is free for a
 * file_ops implementation to use for anything `node` doesn't fit —
 * a pipe stores the pointer to its shared ring buffer there.
 */
struct file {
    const struct file_ops *ops;
    uint64_t node;
    uint64_t off;
    int flags; /* O_* as given to open */
    int refs;  /* fd-table references, vfs.c-owned */
    void *priv;
};

/*
 * Mount the graphfs root (writable) from the registered block
 * device and initialize devfs. Requires kmalloc and the scheduler.
 *
 * A disk is not required to boot: if block_root() found no device
 * (no disk driver claimed one — real hardware before an AHCI/NVMe
 * driver exists, or a deliberate disk-less smoke test), the root
 * filesystem is simply left unmounted and devfs still comes up, so
 * /dev/console keeps working. See vfs_has_root(). A device that IS
 * present but carries no valid graphfs is still a panic — that disk
 * was supposed to have a filesystem on it.
 */
void vfs_init(void);

/* Nonzero once vfs_init() mounted a root filesystem; zero if it
 * booted without a disk. Every vfs_* call that needs the root
 * answers -ENODEV instead of crashing when this is false; /dev
 * keeps working either way. Callers that cannot proceed without a
 * root fs (process_run_init, the fs selftest) check this to skip
 * gracefully instead of tripping over a NULL mount. */
int vfs_has_root(void);

/*
 * Resolve `path` and build an open file description. Supported
 * flags: the access mode, O_CREAT/O_EXCL (mode & 07777 for a new
 * file; no umask exists), O_TRUNC, O_APPEND, O_DIRECTORY; other
 * bits are rejected (-EINVAL). Returns 0 with *out holding one
 * reference, or -errno.
 */
long vfs_open(const char *path, int flags, unsigned mode, struct file **out);

/*
 * Namespace mutation (graphfs only; devfs is immutable, mount
 * points are busy). All follow POSIX/Linux error conventions;
 * see the unlink -EBUSY policy in the header comment. rename
 * across the devfs boundary is -EXDEV; link targets must be
 * regular files (-EPERM for directories, as Linux).
 */
long vfs_mkdir(const char *path, unsigned mode);
long vfs_rmdir(const char *path);
long vfs_unlink(const char *path);
long vfs_link(const char *oldpath, const char *newpath);
long vfs_rename(const char *oldpath, const char *newpath);

/* Boot-time introspection for the in-kernel fs selftest: current
 * generation and free-space counters of the root graphfs. */
void vfs_stats(uint64_t *generation, uint64_t *free_blocks, uint64_t *free_nodes);

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

/*
 * Anonymous pipe (kernel/fs/pipe.c): allocates one shared ring buffer
 * and two open file descriptions over it, unattached to any mount —
 * there is no path a pipe can be opened by. On success *read_out and
 * *write_out each hold one reference; the only failure is -ENOMEM.
 */
long pipe_open(struct file **read_out, struct file **write_out);

/* socketpair(2) domain/type: the only combination kernel/fs/socket.c
 * accepts. Values match the Linux ABI. */
#define AF_UNIX     1
#define SOCK_STREAM 1

/*
 * Local socket pair (kernel/fs/socket.c): validates domain/type/
 * protocol (only AF_UNIX/SOCK_STREAM/0 is supported; anything else is
 * -EAFNOSUPPORT/-EPROTONOSUPPORT) and allocates two open file
 * descriptions, each full-duplex, each able to read what the other
 * writes. On success *a_out and *b_out each hold one reference; the
 * only other failure is -ENOMEM.
 */
long socketpair_open(int domain, int type, int protocol, struct file **a_out, struct file **b_out);
