# The VFS: One Namespace, Open Files, and devfs

Slice 5c gives processes files. This document is the contract for the virtual
filesystem layer: mounts and path resolution, the open-file abstraction and
per-process fd tables, the devfs device nodes, and the locking that lets every
process reach the disk. Chapter 14 of the book is the reasoning behind these
decisions; this is the reference. The layer below is graphfs (Appendix K) over
the block layer (Appendix I); the layer above is the syscall surface
(Appendix H).

## The objects

Three objects, defined in `kernel/include/vfs.h`:

- **`struct file_ops`** — a five-slot vtable: `read`, `write`, `lseek`,
  `fstat`, `getdents`. Every open object is fully described by one ops table
  plus a node id; nothing above the filesystems switches on file type. A
  `NULL` slot means "does not apply," and the syscall layer maps it to a fixed
  errno — the error vocabulary is part of the interface:

  | NULL slot | errno | rationale |
  |---|---|---|
  | `read` | `-EINVAL` | object is not readable |
  | `write` | `-EBADF` | Linux's answer for a read-only description |
  | `lseek` | `-ESPIPE` | the console is a stream, like a pipe |
  | `getdents` | `-ENOTDIR` | only directories enumerate |
  | `fstat` | — | mandatory; every ops table implements it |

- **`struct file`** — an *open file description* (the POSIX term): ops
  pointer, node id (graphfs node or devfs minor), byte **offset**, the `O_*`
  flags from open, and a reference count. The description — offset included —
  is shared by every fd that `fork` duplicated; it is freed when the last
  reference closes. Refcount updates run under `cpu_irq_save` (single CPU).

- **The mount table** — compile-time in v1: the graphfs on the registered
  block device is `/` (`st_dev` 1), devfs is `/dev` (`st_dev` 2). Resolution
  is a longest-prefix match on the canonical path; whatever no mount claims
  belongs to the root filesystem. `/dev` also exists on the graphfs image as
  an empty directory (`graphfs_mkfs --dir /dev`), a conventional mount point:
  the mount covers it, but `ls /` still lists it.

The **fd table** lives with the process (`struct process`, Appendix H):
`FD_MAX` = 16 pointers. `fork` copies the pointers and takes a reference each;
`execve` leaves the table alone (no close-on-exec flags yet); exit closes
everything. Init starts with fds 0/1/2 all referencing one open of
`/dev/console` — one description, three references.

## Path normalization

Every path entering the VFS is first normalized lexically by
`vfs_path_norm()` (`kernel/fs/vfs_path.c` — pure C, host-tested under
ASan/UBSan in `tests/host/test_vfs_path.c`):

- Duplicate slashes collapse; `.` and empty components disappear; a trailing
  slash is dropped; the output is always canonical and absolute.
- `..` pops the previous component; popping above the root stays at the root
  (POSIX: `/..` = `/`).
- Relative paths resolve from `/` — there is no `chdir` yet, so every
  process's working directory is *defined* to be `/`.
- Errors: `-EINVAL` for NULL/empty input, `-ENAMETOOLONG` for a component
  over `VFS_NAME_MAX` (255) or a result over `VFS_PATH_MAX - 1` (255) bytes.

Filesystems therefore never see `.` or `..`; `gfs_resolve()` stays a plain
component walk. **Lexical `..` is correct, not merely convenient, because the
graphfs v1 namespace is a strict tree with no symlinks** — every directory has
exactly one name, so dropping the last component of a canonical path *is* its
parent. If symlinks ever land, normalization must move into the resolution
walk; the comment at the top of `vfs_path.c` pins this dependency.

## open, and what each object supports

`vfs_open()` accepts the access mode plus `O_DIRECTORY`; any other flag
(`O_CREAT`, ...) is `-EINVAL` until the write path. The root mount is
read-only in 5c: opening a disk file `O_WRONLY`/`O_RDWR` is `-EROFS`, opening
any directory for write is `-EISDIR`, and `O_DIRECTORY` on a file is
`-ENOTDIR`.

| object | read | write | lseek | getdents | fstat mode |
|---|---|---|---|---|---|
| graphfs file | yes | — (`-EBADF`) | SET/CUR/END | — (`-ENOTDIR`) | `S_IFREG` |
| graphfs directory | `-EISDIR` | — | SET/CUR/END | yes | `S_IFDIR` |
| `/dev` directory | `-EISDIR` | — | SET/CUR | yes | `S_IFDIR \| 0755` |
| `/dev/console` | blocking | console | — (`-ESPIPE`) | — | `S_IFCHR \| 0666`, rdev 5:1 |

Offsets may seek past EOF (reads there return 0); a negative resulting offset
is `-EINVAL`. `fstat` fills the Linux x86_64 144-byte `struct stat`
(`kernel/include/stat.h`, `_Static_assert`ed): `st_ino` is the graphfs node id
or devfs minor, `st_blocks` counts 512-byte units, `st_blksize` is 4096.

`getdents64` emits real `linux_dirent64` records (8-byte aligned, `d_type`
filled). The file offset is a cursor, not a byte position: 0 is `.`, 1 is
`..`, then one position per outgoing edge. Non-NAME edges (TAG/REF — the
Phase 6 semantic layer) are not namespace entries and are skipped. A buffer
too small for even one record returns `-EINVAL` (Linux semantics); end of
directory returns 0. For `..` of the root, the root is its own parent, and
`/dev`'s `..` is the graphfs root — the two-namespace seam a caller never
sees.

Error mapping from the graphfs core is centralized (`gfs_errno`): `GFS_ENOENT
→ -ENOENT`, `GFS_ENOTDIR → -ENOTDIR`, `GFS_EISDIR → -EISDIR`,
`GFS_ENAMETOOLONG → -ENAMETOOLONG`, `GFS_EROFS → -EROFS`, `GFS_EINVAL →
-EINVAL`, and everything else — device failure, `GFS_EBADCRC`, detected
corruption — is `-EIO`: the data cannot be served, whatever the cause.

## devfs

`kernel/fs/devfs.c` is deliberately tiny: the `/dev` directory (three static
dirents: `.`, `..`, `console`) and `/dev/console`, the kernel console (serial
+ VGA) for output and the PS/2 keyboard for input. Node ids are devfs-local
(1 = directory, 2 = console) under `st_dev` 2, so they never collide with
graphfs inos. A path *through* a device (`/dev/console/x`) is `-ENOTDIR`, not
`-ENOENT`.

Console reads are terminal-style: **block until at least one character is
buffered, then return what is there** (a short read). The lost-wakeup race —
keyboard interrupt firing between "buffer empty" and "sleep" — is closed the
same way `wait4` closes it: the reader publishes itself and blocks inside one
interrupts-off section, and the keyboard IRQ handler wakes the published
reader through a one-line notify hook (`keyboard_set_notify`). A `read_lock`
mutex serializes concurrent readers so the single waiter slot suffices.

## Locking

The kernel's first sleeping lock (`kernel/sched/mutex.c`, `struct mutex`)
exists because disk I/O takes milliseconds with interrupts on, and after 5c
every process can reach the disk. Semantics:

- Contenders queue FIFO and block at scheduler level (no spinning); the
  waiter queue reuses `thread->next`, which a blocked thread is provably not
  using.
- `mutex_unlock` **hands the lock to the oldest waiter** before waking it —
  the lock is never observably free while anyone queues, so arrivals cannot
  barge past sleepers: FIFO fairness and starvation-freedom by construction.
- Thread context only (never an IRQ handler), non-recursive (relock by the
  owner asserts), unlock only by the owner. `mutex_held()` backs assertions.

One global `fs_lock` inside `vfs.c` serializes every graphfs core call and
every graphfs file-offset update — the `struct gfs` scratch buffers are
single-caller by contract. It is held for the duration of one operation,
never across a return to userspace. This lock also discharges the block
layer's documented "one caller at a time" rule for all runtime disk paths
(Appendix I); boot-time callers (`block_selftest`, the mount) run before
userspace exists. Coarse by design: one disk, polled I/O — a per-filesystem
lock buys nothing until there is concurrency to win back.

## exec from disk

`vfs_read_file(path, &buf, &size)` resolves a path to a DATA node and reads
the whole file into a fresh kmalloc buffer (empty files yield a 1-byte buffer
and size 0; a directory is `-EISDIR`). `process_execve()` and
`process_run_init()` load ELF images through it — the Phase 4 built-in program
table and the embedded `kernel/user_blob.asm` blob are deleted. The boot
marker proves it:

```
vfs: graphfs root mounted ro (gen 11, 4081/4096 blocks free, 1018/1024 nodes free)
vfs: devfs at /dev (console)
user: launching init (/bin/init from disk, 13448 bytes)
```

## Verification

- `vfs_path_norm` is host-tested exhaustively (canonical forms, `..` at every
  position, both `-ENAMETOOLONG` causes, `-EINVAL`).
- Init's acceptance suite (Appendix H) exercises the whole read surface from
  ring 3 — every syscall, every error contract, `getdents64` against the known
  image manifest, `/dev/../bin/./hello` through the normalizer — and the boot
  integration test asserts the markers and `status 0`.
- `graphfs_fsck` runs against `fs.img` in `make check`, so the image the
  kernel mounts is independently verified.

## Known limits of this slice (by design, lifted in later slices)

- The root mount is read-only; `write`, `mkdir`, `link`, `unlink`, `rename`,
  `fsync` and remount-writable are slice 5d.
- Mounts are compile-time; a `mount(2)` syscall is far future.
- No `chdir`/cwd, no `dup`/`dup2`, no `O_CLOEXEC`, `FD_MAX` = 16.
- One global filesystem lock; revisit when I/O stops being polled (or SMP).
- Lexical `..` normalization is valid only while the namespace is a strict
  tree with no symlinks (graphfs v1 policy).
- One console, one waiter slot; concurrent readers serialize.
