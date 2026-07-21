# Chapter 14 — The VFS: One Namespace Over Many Filesystems

The previous two chapters built a disk and a filesystem format. What they did not
build is the thing a process actually uses: `open("/bin/hello", O_RDONLY)`. Between
"graphfs can resolve a path" and "a ring 3 program reads a file through a small
integer" sits the **virtual filesystem** — the layer that owns the namespace, the
open-file abstraction, and the file descriptor table, and that lets `/dev/console`
(a device) and `/bin/init` (a graphfs node) answer the same syscalls without the
caller knowing the difference. This chapter is that layer, and it is also the
chapter where the kernel grows its first *sleeping* lock and loads init from disk,
retiring the last scaffold from Phase 4.

## 14.1 Three objects, one discipline

The VFS (`kernel/fs/vfs.c`) is built from three small objects, each with one job:

- **`struct file_ops`** — a vtable of five operations (`read`, `write`, `lseek`,
  `fstat`, `getdents`). This is polymorphism in plain C: a graphfs regular file, a
  graphfs directory, the console device, and the `/dev` directory are four ops
  tables, and everything above them dispatches through the pointer without a
  single `if (is_device)` anywhere in the syscall layer. A `NULL` slot means "this
  operation does not apply here," and the syscall layer maps it to the
  conventional errno — `write` on a directory is `-EBADF`, `lseek` on the
  console is `-ESPIPE`, `getdents64` on a regular file is `-ENOTDIR`. The error
  vocabulary is part of the interface, decided once, at the boundary.
- **`struct file`** — an *open file description*, the POSIX term worth being
  precise about: it holds the ops pointer, the object identity, the **offset**,
  and a reference count. It is not the fd. When a process forks, parent and child
  fds point at the *same* description — read in one and the other's offset moves.
  That is not an accident of implementation; it is the POSIX contract that makes
  `fork` + shared logs work, and getting it right costs exactly one refcount.
- **The mount table** — compile-time in v1: graphfs on the root block device is
  `/`, and devfs covers `/dev`. Resolution is a longest-prefix match on the
  canonical path; whatever no mount claims belongs to the root filesystem. The
  `/dev` directory also exists *on disk* as an empty graphfs namespace
  (`graphfs_mkfs --dir /dev`), the way a mount point does on any Unix — the mount
  covers it, but `ls /` still lists it, because the namespace under a mount point
  is the on-disk one.

The fd table itself lives with the process (`struct process`, Chapter 11): a
small array of `struct file *`. `fork` duplicates the pointers and bumps
refcounts; `execve` deliberately leaves the table alone (no close-on-exec flags
yet — documented); exit closes everything. Init starts life with fds 0/1/2 all
referencing one open of `/dev/console` — one description, three references,
exactly what a login shell would inherit.

## 14.2 Path walking: the one pure piece, and why lexical ".." is honest here

Every path entering the VFS is first **normalized lexically** by
`vfs_path_norm()` (`kernel/fs/vfs_path.c`): duplicate slashes collapse, `.`
disappears, `..` pops a component, popping above the root stays at the root, and
the output is always a canonical absolute path. The function is pure C over two
buffers — no kernel dependencies — so it is host-tested under ASan/UBSan
(`tests/host/test_vfs_path.c`) with a table of canonical forms and every
rejection path, like every other core in the tree. Filesystems never see a `.` or
`..`: `gfs_resolve()` stays the simple plain-component walk Chapter 13 built.

Be honest about *when* this is correct. Lexical `..` handling is exactly right
here **because the graphfs v1 namespace is a strict tree with no symlinks**:
every directory has one name, so dropping the last component of a canonical path
*is* its parent. The day symlinks arrive, `/a/b/..` may no longer be `/a`, and
normalization has to move into the resolution walk. The comment at the top of
`vfs_path.c` says precisely this — a design decision pinned to the invariant that
justifies it, so the person who breaks the invariant finds the consequence
written where they will trip over it.

One more scoping decision of the same kind: there is no `chdir` yet, so every
process's working directory is defined to be `/` and relative paths resolve from
there. Not "relative paths are rejected," not "undefined" — defined, documented,
and replaced when cwd machinery arrives.

## 14.3 The kernel's first sleeping lock

Until now the kernel's only mutual exclusion was interrupts-off sections —
correct on one CPU, and fine for microsecond-scale work (Chapter 9). Disk I/O
breaks that model: a block read takes *milliseconds* and must keep interrupts on
(the driver polls a timer). Meanwhile the `struct gfs` handle is single-caller by
contract — its block scratch buffers cannot host two walks at once — and after
this chapter, *every process in the system* can reach the disk through a syscall.
Two forked children execing simultaneously would interleave core calls and
corrupt the walk state.

The answer is the kernel's first **sleeping mutex** (`kernel/sched/mutex.c`),
~70 lines over the primitives Chapter 9 already proved: contenders queue FIFO and
`sched_block()`; `mutex_unlock()` *hands the lock to the oldest waiter* before
waking it. The handoff detail is worth noticing — the lock is never observably
free while anyone queues, so a fresh arrival cannot barge past a sleeper, which
is FIFO fairness and starvation-freedom in two lines. The waiter queue reuses
`thread->next`, which a blocked thread is provably not using — the same
"a thread is on at most one list" invariant from Chapter 9, cashed in.

One global `fs_lock` inside `vfs.c` serializes every graphfs core call and every
file-offset update. That single lock also **discharges the block layer's
documented debt**: Chapter 12 left `block.c` asserting "one caller at a time"
with a note that the VFS would bring the real lock, and now it has — the comment
in `block.h` records the discharge. A promissory note in a comment is only
honest if someone actually pays it; slice 5c was where this one came due.

## 14.4 devfs and the blocking read: the lost-wakeup pattern, third appearance

devfs (`kernel/fs/devfs.c`) is deliberately tiny: `/dev/console` and the `/dev`
directory itself, each an ops table and a synthetic inode number. Console writes
go to the kernel console. Console *reads* are the interesting part: they must
**block** until the keyboard produces input — the first time a syscall sleeps on
a device.

The race to beat is the same lost wakeup that `wait4` fought in Chapter 11: if
the reader checks the buffer (empty), and the keyboard interrupt fires *before*
the reader sleeps, the wakeup is lost and the reader sleeps forever. The cure is
also the same, because it is *the* cure: publish the waiter and block inside one
interrupts-off section. The keyboard IRQ handler gets a one-line notify hook
(`keyboard_set_notify`) that wakes the published reader. By the third appearance
— join, wait4, now console read — the pattern should feel like an instinct:
**check-and-sleep must be atomic against the waker.**

Read semantics are terminal-style: block until at least one character exists,
then return what the buffer holds (a short read), because an interactive caller
asking for 100 bytes wants the 3 you have now, not a wait for 97 more. A second
lock (`read_lock`) serializes concurrent readers so the single waiter slot
suffices — a scoped simplification that holds until someone actually wants
competing console readers.

## 14.5 exec from disk: the scaffold comes down on schedule

Chapter 2 called the ELF images embedded in kernel `.rodata` "an honest scaffold
with a clear expiry date." This is the expiry date. `process_execve()` and
`process_run_init()` now call `vfs_read_file()` — resolve, size, read the whole
image into a kernel buffer, load, free — and the built-in program table,
`kernel/user_blob.asm`, and the `incbin` build rule are *deleted*. The boot
marker changes from `launching init (embedded ELF)` to:

```
user: launching init (/bin/init from disk, 13488 bytes)
```

and the integration test asserts the new text — the boot now *proves* init came
off the graphfs image, because there is no other place it could come from.

The syscall surface grows to match, and every new syscall keeps the Linux x86_64
ABI bit-for-bit: `read` 0, `open` 2, `close` 3, `fstat` 5, `lseek` 8,
`getdents64` 217, alongside the existing numbers. `struct stat` is the Linux
144-byte layout (`_Static_assert`ed), `getdents64` emits real `linux_dirent64`
records, and directories synthesize `.` and `..` entries the way readdir
consumers expect. None of this costs more than inventing a private layout would,
and it is exactly what makes the Phase 7 personality layer a numbering no-op
(Appendix H tabulates the whole surface).

## 14.6 Proving it: the acceptance suite grows 27 checks

Init's role as the boot acceptance test (Chapter 11) extends to the whole VFS
read side — checks 21 through 47 open a known file and verify its ELF magic,
prove `lseek END/SET` against `fstat`'s size, walk `/bin` with `getdents64` and
require *exactly* `.`, `..`, `init`, `hello` and nothing else, then probe every
error contract: `-ENOENT` for a missing name, `-ENOTDIR` for a path through a
file, `-EISDIR` for reading a directory, `-EBADF` after close, `-ESPIPE`
for seeking the console. A path like `/dev/../bin/./hello` must resolve — the
normalizer's host tests re-proven end-to-end from ring 3. The exit status names
the first failure; the boot test asserts the serial markers and `status 0`.

## 14.7 The write path: teaching the namespace to change

The read side is the half you can prove without being able to mutate anything.
Slice 5d mounts the root writable and adds the other half — `write`, `O_CREAT`
and friends, `mkdir`/`rmdir`, `link`/`unlink`, `rename`, `fsync` — and three
of its decisions are worth studying, because each one is a *policy* forced to
the surface by an invariant.

**Creation is atomic because fsck says so.** The graphfs core had
`gfs_node_create` and `gfs_link` since slice 5b, and composing them would
implement `mkdir` in two calls — with a crash window between them that leaves
an allocated, unreferenced node. For a directory, that orphan is precisely
what `gfs_fsck` flags as corruption ("namespace has 0 parents"). So the core
grew `gfs_create_at`: create *and* link in one transaction, the node born
with its name. The checker defined what the mutation path was allowed to look
like — write your verifier first and it will design your API.

**Unlinking an open file is `-EBUSY`, and the deviation is pinned.** POSIX
keeps an unlinked-but-open file alive until its last close. Honoring that
means on-disk orphan tracking (the node must survive a crash between unlink
and close without leaking), which is real machinery for semantics nothing in
this system needs yet. The alternatives were: implement it, leak nodes across
crashes, or — worst — free the node while an fd still points at it and let
the id be reused under a live file description. The kernel refuses exactly
the dangerous case (`nlink == 1` and open descriptions exist), documents the
deviation where the contract lives, and notes what will lift it (Phase 7).
Hard links keep the common temp-file shapes expressible. A documented honest
"no" beats a silent wrong "yes."

**Rename's cycle check rides on the namespace invariant.** Moving `/a` into
`/a/b/c` would detach a subtree into an unreachable cycle. Detecting that in
a general graph means a reachability walk; in graphfs v1 it is a loop over
single-parent fields from the destination up to the root — cheap because the
5b policy decision ("namespaces have exactly one parent") already paid for
it. Invariants compound: `..` as one field, no cycles possible, and now an
O(depth) rename guard, all from the same choice.

And the gate promised in 5c arrives: after every boot test, `run_qemu.py`
runs `graphfs_fsck` over the image the guest actually wrote — the block
selftest, the in-kernel stress cycles (create/write/rename/unlink with
free-space accounting asserted back to the starting counts), and init's
write-path checks (48 through 85, from `O_CREAT|O_EXCL` through the
`-EBUSY`/`-EXDEV`/`-ENOTEMPTY` vocabulary, in a scratch tree init removes
without a trace). Every `make check` is now a crash-consistency test of the
write path, forever.

## 14.8 Booting without a disk: the last panic that had to go

Every layer below the VFS already knew how to say "not present" instead of
falling over: `virtio_blk_init` logs `virtio-blk: no device` and returns,
`block_selftest` logs `block: selftest skipped (no device)` and returns. Only
`vfs_init` still panicked — `if (bd == NULL) panic(...)` — because at the
time it was written, nothing could run without a root filesystem, so a
missing disk *was* fatal. That stopped being true the moment real hardware
entered the picture: this kernel is going to boot on physical hardware
(Appendix M) before an AHCI or NVMe driver exists to give it a disk at all,
and a panic on the very first boot attempt is a wasted trip to the machine
for a failure that was knowable in advance.

The fix is smaller than it sounds, because the shape was already right one
layer down. `vfs_init` mirrors the pattern: no device means devfs still
comes up (`/dev/console` needs no disk) and the function returns instead of
mounting anything. But `vfs_init` isn't a leaf — a dozen call sites assumed a
mounted root without checking, from `vfs_open`'s fallthrough to graphfs down
to `vfs_read_file`, and two boot-time callers (`process_run_init`, the fs
selftest) needed to *ask* rather than find out by crashing. The honest fix
touches every one of them: a `vfs_has_root()` accessor, an `-ENODEV` guard at
each entry point that would otherwise dereference a null mount, and two call
sites that check first and skip loudly instead of finding out through a
kernel panic on an `ENOENT` no one expected. `process_run_init` in particular
now says exactly what happened — `process: no root filesystem, skipping
init` — instead of the caller-visible symptom being "init failed to load."

The payoff is a genuine third boot configuration, not just a code path that
compiles: `make check-boot-nodisk` boots the disk image with no virtio-blk
device attached at all and asserts the kernel still reaches
`boot: complete` having panicked nowhere, on every commit, forever — the same
discipline that turned the write path into a permanent crash-consistency
test now covers "what does this kernel do with no disk," months before there
was hardware to try it on.

## 14.9 The transferable lessons

- **An ops vtable plus a refcounted description is the whole VFS trick.** Four
  ops tables, one dispatch site, POSIX offset-sharing from one refcount — no
  type switches anywhere above the filesystems.
- **Pin a lexical shortcut to the invariant that makes it sound.** Lexical `..`
  is *correct* under a strict tree with no symlinks; the comment names the
  invariant so its removal takes the shortcut with it.
- **When critical sections learn to sleep, interrupts-off stops being a lock.**
  Millisecond I/O plus multiple callers forced the first real mutex — and
  unlock-by-handoff bought fairness for free.
- **Pay your documented debts, and record the payment.** The block layer's
  "single caller until the VFS" note was a promissory note; the discharge is
  written where the promise was.
- **The same race deserves the same cure, every time.** Publish-then-block
  interrupts-off killed the lost wakeup in join, wait4, and now console read.
  Collect patterns, not incidents.
- **Let the acceptance suite grow with the surface.** Every new syscall landed
  with checks a boot cannot pass without executing them.
- **Let the checker design the mutation.** `gfs_create_at` exists because
  fsck's definition of a healthy image made two-call creation wrong; the
  verifier was the design document.
- **Deviate loudly or not at all.** The `-EBUSY` unlink policy is a
  deliberate, written-down departure from POSIX with its lifting condition
  named — the opposite of a quiet shortcut.
- **A hard requirement changes what "done" means for code you already
  shipped.** Nothing about `vfs_init`'s panic was wrong when Phase 5 landed;
  it became wrong the moment "runs on real hardware" turned from a goal into
  a constraint every boot path has to satisfy, including the one where the
  disk driver that constraint depends on doesn't exist yet.

The kernel now walks one namespace from `/` to devices and disk files,
nothing it runs is embedded in it, and the namespace changes under it —
created, written, linked, renamed, unlinked — while every boot proves the
disk image stayed perfectly consistent. Phase 5 is complete: the storage
stack runs from PCI enumeration to POSIX semantics, and the crash-consistency
gate graphfs's copy-on-write design was built to pass now runs on every
`make check`.
