# The VFS: One Namespace, Open Files, and devfs

Slice 5c gives processes files; slice 5d lets them change the filesystem.
This document is the contract for the virtual filesystem layer: mounts and
path resolution, the open-file abstraction and per-process fd tables, the
devfs device nodes, the write path and its policies, and the locking that
lets every process reach the disk. Chapter 14 of the book is the reasoning
behind these decisions; this is the reference. The layer below is graphfs
(Appendix K) over the block layer (Appendix I); the layer above is the
syscall surface (Appendix H).

## The objects

Three objects, defined in `kernel/include/vfs.h`:

- **`struct file_ops`** — six operation slots plus one lifecycle hook:
  `read`, `write`, `lseek`, `fstat`, `getdents`, `fsync`, and `release`
  (optional, called once right before the description is freed — pipes are
  the first user, see below). Every open object is fully described by one
  ops table plus a node id; nothing above the filesystems switches on file
  type. A `NULL` operation slot means "this *object* does not support the
  operation," and the syscall layer maps it to a fixed errno — the error
  vocabulary is part of the interface:

  | NULL slot | errno | rationale |
  |---|---|---|
  | `read` | `-EINVAL` | object is not readable |
  | `write` | `-EBADF` | directories never open for writing |
  | `lseek` | `-ESPIPE` | the console is a stream, like a pipe |
  | `getdents` | `-ENOTDIR` | only directories enumerate |
  | `fsync` | `-EINVAL` | special file with nothing to sync (as Linux) |
  | `fstat` | — | mandatory; every ops table implements it |

  Whether this *description* may read or write — the access mode given to
  open — is enforced inside the ops themselves with `-EBADF`: a graphfs
  file's `write` slot is always populated, and refuses an `O_RDONLY`
  description at call time.

- **`struct file`** — an *open file description* (the POSIX term): ops
  pointer, node id (graphfs node or devfs minor), byte **offset**, the `O_*`
  flags from open, a reference count, and `priv` — a `void *` free for a
  file_ops implementation to use for anything `node` doesn't fit (a pipe's
  shared ring buffer, see below). The description — offset included — is
  shared by every fd that `fork` duplicated; it is freed when the last
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

`vfs_open()` accepts the access mode plus `O_CREAT`, `O_EXCL`, `O_TRUNC`,
`O_APPEND`, and `O_DIRECTORY`; any other flag is `-EINVAL` — unimplemented
flags are refused, never silently ignored. The Linux corner cases are kept:
`O_DIRECTORY | O_CREAT` is `-EINVAL`, `O_CREAT` or `O_TRUNC` on an existing
directory is `-EISDIR`, `O_TRUNC` truncates even on an `O_RDONLY` open, and
`O_DIRECTORY` on a file is `-ENOTDIR`.

`O_CREAT` creates a missing regular file (mode `& 07777`; there is no umask)
in one atomic graphfs transaction — resolve, create, and link happen under a
single lock hold, so two racing creators cannot both succeed with `O_EXCL`.
`O_APPEND` makes every write land at the current EOF: the size read and the
write share one lock hold, so appends are atomic. Files created or truncated
this way behave identically to mkfs-installed ones.

| object | read | write | lseek | getdents | fsync | fstat mode |
|---|---|---|---|---|---|---|
| graphfs file | mode-checked | mode-checked | SET/CUR/END | — (`-ENOTDIR`) | 0 | `S_IFREG` |
| graphfs directory | `-EISDIR` | — (`-EBADF`) | SET/CUR/END | yes | 0 | `S_IFDIR` |
| `/dev` directory | `-EISDIR` | — | SET/CUR | yes | — (`-EINVAL`) | `S_IFDIR \| 0755` |
| `/dev/console` | blocking | console | — (`-ESPIPE`) | — | — (`-EINVAL`) | `S_IFCHR \| 0666`, rdev 5:1 |

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
-EINVAL`, `GFS_EEXIST → -EEXIST`, `GFS_ENOTEMPTY → -ENOTEMPTY`, `GFS_EFBIG →
-EFBIG`, `GFS_ENOSPC` *and* `GFS_EFRAG` `→ -ENOSPC` (however the space ran
out, the write does not fit), `GFS_EMANYPARENTS → -EPERM` (Linux's answer to
`link(2)` on a directory), and everything else — device failure,
`GFS_EBADCRC`, detected corruption — is `-EIO`: the data cannot be served,
whatever the cause.

## The write path

The root is mounted writable; every mutating call is one graphfs CoW
transaction that is durable before the syscall returns (Appendix K). The
namespace syscalls map onto the VFS entry points:

- **`mkdir`** — `gfs_create_at(parent, name, NAMESPACE)`: creation and
  linking are a single transaction, because fsck rightly treats an orphaned
  directory as corruption; there is no crash window in which a node exists
  without its name. `O_CREAT` uses the same call with a DATA node.
- **`rmdir`** — refuses a non-directory (`-ENOTDIR`), a non-empty directory
  (`-ENOTEMPTY`, enforced by the core), the root and mount points
  (`-EBUSY`), and a directory some fd holds open (`-EBUSY`).
- **`unlink`** — refuses directories (`-EISDIR`; `rmdir` is the tool). A
  DATA node whose last name is removed is freed with its blocks — unless it
  is nlink > 1 (a hard link keeps it alive under its other names).
- **`link`** — hard links, DATA nodes only (`-EPERM` for directories, as
  Linux — the single-parent namespace invariant is load-bearing: it is what
  makes `..` one field and cycles impossible). Cross-mount is `-EXDEV`.
- **`rename`** — atomic in one transaction, with POSIX replace semantics: a
  file replaces a file, a directory replaces an *empty* directory, the
  replaced node is freed if that was its last name. Renaming onto a hard
  link of the same node is a no-op. Moving a directory into its own subtree
  is `-EINVAL`, detected by walking the destination's single-parent chain.
  A moved directory's parent pointer is rewritten in the same transaction.
- **`fsync`** — returns 0 on any graphfs object: the block cache is
  write-through and every transaction commits before returning, so there is
  nothing buffered anywhere to flush. The call exists so programs can *ask*
  for durability and get an honest answer.

**The open-unlink policy (v1, pinned deliberately).** Removing the *last*
name of a file that some open description still references is `-EBUSY`.
POSIX instead keeps the file alive until the last close; that requires
on-disk orphan tracking to survive a crash between the unlink and the close
(or it leaks nodes), which is real machinery for a semantics nothing in this
system needs yet. The VFS keeps an open-description count per node
(`node_opens[]`, maintained under the filesystem lock from open to last
close) and refuses exactly the dangerous case: `nlink == 1 && opens > 0`.
Unlinking one name of a multiply-linked open file works, so the temp-file
shapes that matter are expressible with a hard link. Replacing an open file
via `rename` answers the same `-EBUSY`. Revisited when the Linux personality
layer (Phase 7) needs the real semantics.

Namespace mutation respects the mount table: `/` and `/dev` (a mount point)
refuse removal and renaming with `-EBUSY` (`mkdir` on them is `-EEXIST`),
anything *under* `/dev` is `-EPERM` — devfs is structurally immutable — and
`rename`/`link` across the graphfs/devfs seam is `-EXDEV`.

## devfs

`kernel/fs/devfs.c` is deliberately tiny: the `/dev` directory (three static
dirents: `.`, `..`, `console`) and `/dev/console`, the kernel console (serial
+ VGA) for output and the PS/2 keyboard for input. Node ids are devfs-local
(1 = directory, 2 = console) under `st_dev` 2, so they never collide with
graphfs inos. A path *through* a device (`/dev/console/x`) is `-ENOTDIR`, not
`-ENOENT`. devfs cannot grow nodes: `O_CREAT` on a missing name directly
under `/dev` is `-EPERM` (a missing *intermediate* directory stays plain
`-ENOENT`); `O_EXCL` against an existing device is `-EEXIST`; `O_TRUNC` on
the console is ignored, as Linux ignores it on character devices.

Console reads are terminal-style: **block until at least one character is
buffered, then return what is there** (a short read). The lost-wakeup race —
keyboard interrupt firing between "buffer empty" and "sleep" — is closed the
same way `wait4` closes it: the reader publishes itself and blocks inside one
interrupts-off section, and the keyboard IRQ handler wakes the published
reader through a one-line notify hook (`keyboard_set_notify`). A `read_lock`
mutex serializes concurrent readers so the single waiter slot suffices.

## Pipes

`kernel/fs/pipe.c` (Phase 6) is the first VFS object with no path and no
mount: `pipe(2)` (syscall 22, Appendix H) allocates one shared 4 KiB ring
buffer and two open file descriptions over it — a read end and a write end —
directly, without going through `vfs_open`'s mount table at all. It exists
to give the file_ops vtable its first user *beyond* graphfs and devfs, and
the shape of what that took is the reusable part:

- **`pipe_core.c`** is the ring buffer's index arithmetic (wraparound,
  short reads/writes when the buffer under- or over-runs the request),
  pure and host-tested exactly like `graphfs_core`/`virtq_core` — no
  allocation, no blocking, no kernel dependency at all.
- **`struct file` grew a `priv` field** (`void *`, alongside the existing
  `node`) because a pipe's two ends need to reach the *same* shared ring
  buffer, and that buffer isn't identified by a filesystem-scoped id the
  way `node` is for graphfs and devfs — it needs a raw pointer.
- **`struct file_ops` grew an optional `release` hook**, called once from
  `vfs_file_put` right before the description is freed. graphfs and devfs
  still don't use it (their per-node bookkeeping lives inline in
  `vfs_file_put`); a pipe end uses it to decrement its shared buffer's
  reader or writer count and wake the other side, which is what turns
  "the last copy of the other end closed" into `EOF` (read) or `-EPIPE`
  (write) instead of a leaked `struct pipe` or a wakeup nobody delivers.

**Locking is deliberately not the VFS's sleeping `fs_lock`.** A pipe
operation is a memory copy, not disk I/O — there is nothing slow to hold a
lock across — so it follows the PMM/heap pattern instead: a plain
interrupts-off critical section around the ring-buffer manipulation and the
wait-queue push. `sched_block()` is called from inside that section, which
is exactly what closes the lost-wakeup race (publish the wait, then block,
without interrupts re-enabling in between) — the same contract devfs's
console read and `wait4` rely on, reused a third and fourth time (two wait
queues: readers block on empty, writers block on full). The wait queues
reuse `thread->next`, exactly as `struct mutex`'s FIFO queue does — a thread
blocked on a pipe is provably on no other queue at the same time.

**Semantics**, matching Linux where there is a clean answer: `read` returns
as soon as *any* data is available (a short read, never waits to fill the
whole request) and returns `0` once the buffer is empty and every writer has
closed (`EOF`, not a blocked-forever read); `write` loops internally and
blocks on a full buffer until every requested byte is queued, and returns
`-EPIPE` immediately if every reader has already closed. What isn't
promised: write atomicity for writes larger than one buffer when multiple
writers share a pipe (Linux guarantees this up to `PIPE_BUF`) — nothing in
this kernel puts more than one writer on the same pipe yet, so there is no
observer to violate the guarantee for; and `O_NONBLOCK` / `dup`-based
pipeline wiring (Appendix H's known limits).

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
vfs: graphfs root mounted rw (gen 7, 4081/4096 blocks free, 1018/1024 nodes free)
vfs: devfs at /dev (console)
user: launching init (/bin/init from disk, 17688 bytes)
```

## Booting without a root filesystem

`vfs_init()` no longer treats a missing disk as fatal. `block_root()` returns
`NULL` when no driver claimed a device — the expected state on real hardware
before an AHCI/NVMe driver exists (Appendix I's virtio-blk driver has nothing
to bind to outside QEMU), and the deliberate state of a disk-less smoke-test
boot. In that case `vfs_init()` still brings up devfs (`/dev/console` needs no
disk) and returns without mounting a root:

```
vfs: no block device found — booting without a root filesystem
vfs: devfs at /dev (console)
```

`vfs_has_root()` reports which state the kernel is in. Every VFS entry point
that would otherwise dereference the (now `NULL`) root mount checks it first
and answers `-ENODEV` instead of crashing — `vfs_open` for any path outside
`/dev`, `vfs_mkdir`/`rmdir`/`unlink`/`link`/`rename`, and `vfs_read_file`.
`/dev/console` keeps working either way, because devfs never depends on
`root_fs`.

Two boot-time callers check `vfs_has_root()` themselves rather than relying on
`-ENODEV` propagating: the in-kernel fs selftest (`selftest_run`, Appendix L)
skips `test_fs()` and logs `selftest: fs write-path test skipped (no root
filesystem)`, and `process_run_init()` (Appendix H) logs `process: no root
filesystem, skipping init` and returns without touching the process table —
there is no `/bin/init` to load `/bin/init` from. `kmain` falls through to the
same interactive keyboard-echo loop it would reach after a normal boot, so a
disk-less boot still proves the entire chain up through interrupts, the
scheduler, and the console: exactly what an early real-hardware smoke test
needs before an AHCI driver exists. See Appendix M for how that smoke test is
run on physical hardware.

A device that *is* present but carries no valid graphfs is a different
situation and still panics — that disk was supposed to have a filesystem on
it, and mounting garbage silently would be worse than stopping.

## Verification

- `vfs_path_norm` is host-tested exhaustively (canonical forms, `..` at every
  position, both `-ENAMETOOLONG` causes, `-EINVAL`).
- The graphfs write path — `gfs_create_at`, `gfs_rename` (every replace and
  cycle case), `gfs_truncate` (including the zero-tail invariant), and full
  create/write/rename/unlink accounting round trips — is host-tested under
  ASan/UBSan with fsck after every mutation (`tests/host/test_graphfs.c`).
- The in-kernel fs selftest runs the same cycles at every boot through the
  real VFS onto the real disk, asserting that free blocks and nodes return
  exactly to their starting counts (`selftest: fs write path ok`).
- Init's acceptance suite (Appendix H) exercises the whole surface from
  ring 3 — every syscall, every error contract, `getdents64` against the
  known image manifest, the write path in a scratch tree it removes without
  a trace — and the boot integration test asserts the markers and `status 0`.
- `graphfs_fsck` gates the freshly built `fs.img` in `make check`, **and
  runs again on the image the guest wrote to after every boot test** — every
  boot is a crash-consistency test of the write path.

## Known limits of this slice (by design, lifted in later slices)

- Unlinking the last name of an open file is `-EBUSY`, not deferred
  reclamation (the pinned v1 policy above; revisited in Phase 7).
- Mounts are compile-time; a `mount(2)` syscall is far future.
- No `chdir`/cwd, no `dup`/`dup2`, no `O_CLOEXEC`, no `ftruncate`/
  `fdatasync`, `FD_MAX` = 16. No timestamps until the kernel keeps a clock.
- One global filesystem lock; revisit when I/O stops being polled (or SMP).
- Lexical `..` normalization is valid only while the namespace is a strict
  tree with no symlinks (graphfs v1 policy).
- One console, one waiter slot; concurrent readers serialize.
