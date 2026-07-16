# Chapter 13 — graphfs: A Filesystem From First Principles

The final subsystem is the one you are, at the time of writing, building: the
native filesystem. It is deliberately *not* a clone of ext2. It is a
copy-on-write, self-checksumming **property-graph** filesystem, and this chapter
is as much about *how to design an on-disk format* as it is about this particular
one. Designing a durable, corruption-resistant on-disk layout is one of the
deepest skills in systems programming, because the format is a contract with your
own future self across power failures and years of code changes — and unlike an
API, you cannot refactor it without a migration. The authoritative format spec is
Appendix K; this chapter is the reasoning behind it.

## 13.1 The design decision: a graph, not a tree

Most filesystems model a tree of directories containing files. graphfs models a
**property graph**: everything on disk is either a **node** (content plus
metadata) or a **typed, named edge** between nodes. The POSIX namespace is then
just *one edge type* layered on the graph — a "directory" is a node whose outgoing
`NAME` edges are its entries, and path resolution walks `NAME` edges by name. Two
other edge types, `TAG` and `REF`, are valid on disk today but ignored by path
resolution, reserved for a future AI-native semantic layer (provenance, semantic
links) with **no on-disk format change required**.

This is a strategic design bet identical in spirit to adopting the Linux syscall
ABI: build the substrate now so the future feature is a policy addition, not a
format migration. The most expensive thing to change in a storage system is the
format, so the format is where you pay for foresight. Even if the semantic layer
never ships, the cost was one more field in an edge record; if it does, an entire
class of "we can't do that without reformatting every disk" is avoided.

## 13.2 Copy-on-write: consistency without a journal

The core durability decision is that graphfs is **copy-on-write and never
overwrites live data**, in the ZFS/APFS mould. A change writes *fresh* blocks and
is made visible by a single atomic superblock write. The consequence is exact and
powerful:

> A power loss either lands **before** that superblock write — in which case the
> change never happened — or **after** it — in which case the change is whole.
> There is no journal, and no repair-on-boot fsck, because the on-disk image is
> *always* structurally consistent.

Compare the two classical approaches to crash consistency. A journaling
filesystem writes intended changes to a log first, then applies them, and replays
the log after a crash — correct, but every metadata change is written twice and
recovery is a process. Copy-on-write instead makes the *commit itself* atomic:
because nothing live is ever mutated in place, an interrupted operation leaves the
old version completely intact and simply invisible-in-progress. The design turns
crash consistency from an active recovery procedure into a *structural property*.
That is almost always the better kind of guarantee — one that holds by
construction rather than one you have to execute correctly under duress.

Making "a single superblock write commits everything" true requires that
everything the new state depends on is already durably on disk *before* the
superblock write, and that the superblock write is atomic. graphfs gets the
atomicity from **two superblock slots** selected by `generation & 1`: a commit
always writes the *inactive* slot at `generation + 1`, so the currently-live slot
is never touched, and mount simply takes the valid slot with the highest
generation. The allocation bitmap is likewise **double-buffered** by generation
parity. A commit never writes over anything the live filesystem is currently
relying on — which is exactly what makes an interrupted commit a no-op.

## 13.3 Self-validating checksums: detecting silent corruption

The second modern property is that graphfs is **self-checksumming**, and the
*placement* of the checksums is the clever part:

> Every metadata block is covered by a crc32c stored in the **pointer that
> reaches it**, not in the block itself. The superblock, having no parent,
> checksums itself.

Think about why "checksum in the pointer, not the block" is right. If a block
stored its own checksum, a write that was *misdirected* by the hardware — landing
the right bytes at the wrong address, a real failure mode — would carry a
perfectly valid self-checksum and be silently accepted. By putting each block's
checksum in its *parent* (a `struct gfs_bp = { phys, crc }`), the filesystem forms
a **self-validating tree**: to trust a block you must have arrived at it through a
parent that both named its location and attested its contents. Corruption — bit
rot, a misdirected write, a torn write — is caught on *read*, and served data is
data whose entire path from the superblock down was verified. This is the same
principle ZFS made famous, and it is a strictly stronger integrity guarantee than
in-block checksums.

The honest scope: v1 checksums *all metadata*; data-block checksums are a
documented later extension (exactly as btrfs shipped incrementally). The boundary
is stated, not blurred.

## 13.4 The layout, and what the constants encode

The on-disk geometry (full table in Appendix K, offsets in
`kernel/include/graphfs_core.h`) is: two superblock slots at LBA 0 and 1, then two
allocation-bitmap copies, then a copy-on-write region that begins with a node-map
block and the root directory's node-table block and grows on demand. 4 KiB blocks
throughout — page-sized, block-layer-block-sized, as Chapter 12 argued.

A few structural choices reveal how a format encodes its policies in constants:

- **Nodes are 256-byte records, 16 per block.** The node map is one 4 KiB block of
  16-byte checksummed pointers → at most 256 node-table blocks → **4096 nodes**
  (`GFS_MAX_NODES`). The maximum is not arbitrary; it is what the single-block node
  map can address. When a limit is a clean consequence of a structural choice, it
  is easy to reason about and easy to lift deliberately later (a multi-block node
  map).
- **Files are stored as extents** — runs of contiguous blocks — with **8 inline
  extents per node** and no extent tree in v1. So a file is at most 8 fragments
  (`GFS_EFRAG` past that), each up to 2³²−1 blocks. The cap is on *fragmentation*,
  not size, and a copy-on-write-friendly allocator keeps freshly written files to
  a single extent. This is a deliberate simplicity/capability trade with an
  explicit failure mode, not a lurking truncation bug.
- **Node types and edge types are small integers** (`NAMESPACE`/`DATA`;
  `NAME`/`TAG`/`REF`), and **all integers are little-endian**, stated once and
  obeyed everywhere. An on-disk format lives or dies by this kind of ruthless
  consistency, because every field is read by code that must agree byte-for-byte
  with the code that wrote it — possibly years apart.

## 13.5 Policy that is not format: single-parent namespaces

graphfs draws a careful line between what the *format* permits and what the
current *policy* allows — and understanding that line is a lesson in itself. The
v1 rule: a `NAMESPACE` node has exactly one incoming `NAME` edge (a single
parent), so `..` is a single stored field and directory cycles are *impossible*;
`DATA` nodes may have any number of incoming `NAME` edges (hard links). A second
`NAME` edge onto a namespace is rejected with `GFS_EMANYPARENTS`.

The crucial framing: this is a **link-time policy, not a format limitation.** The
on-disk structures could represent a multi-parent namespace graph perfectly well;
the *code* chooses to forbid it in v1 because single-parent directories make `..`
trivial and cycles unrepresentable, which eliminates a whole category of
path-resolution and garbage-collection hazards. Multi-parent namespaces are a
documented future extension that needs no format change. Distinguishing "the disk
can't represent this" from "the code currently chooses not to allow this" is
something juniors routinely conflate, and the distinction is exactly what tells
you whether a future feature is a policy tweak or a migration.

## 13.6 One format, three programs: the pattern at full strength

graphfs is the culmination of the pure-core discipline the whole book has built
toward. `kernel/fs/graphfs_core.c` is pure C over an abstract block-device
callback (`struct gfs_ops`), with **no kernel dependencies and no dynamic
allocation** — block scratch lives inside `struct gfs`, and the writable allocator
and fsck take caller-supplied buffers. That single implementation compiles into
*three* programs:

1. The **kernel**, which mounts the filesystem over virtio-blk.
2. `tools/graphfs_mkfs`, which creates an image and installs files at `/bin` — the
   build uses it to lay real `/bin/init` and `/bin/hello` onto `fs.img` instead of
   the embedded blob from Chapter 11.
3. `tools/graphfs_fsck`, which verifies an image; `make check-fsck` runs it on the
   freshly built filesystem, and the same check will run after boot as a
   crash-consistency gate.

Plus the host unit tests in `tests/host/test_graphfs.c`, under ASan/UBSan. The
enormous payoff: **the format has exactly one implementation.** mkfs cannot
disagree with the kernel about the layout, because they *are* the same code. fsck
cannot check a different format than the kernel writes. The host tests exercise
the identical bytes the kernel will mount. Every filesystem project's nightmare is
three subtly-diverging implementations of one format (the classic ext2 mkfs/fsck/
kernel drift); the pure-core discipline makes that divergence structurally
impossible. And because a copy-on-write, checksummed image is *always* consistent,
a healthy image *always* passes fsck — so an fsck failure means precisely a core
bug or media corruption, a sharp and useful signal rather than routine cleanup.

## 13.7 The transferable lessons

- **The format is where you pay for foresight.** You can refactor an API; you
  migrate a format. Building the graph substrate and the extra edge types now
  turns a future semantic layer into a policy addition, not a reformat.
- **Prefer guarantees that hold by construction.** Copy-on-write makes crash
  consistency a structural property — an interrupted commit is a no-op — instead
  of a recovery procedure you must execute correctly under duress.
- **Put integrity checksums in the pointer, not the block.** A self-validating
  tree catches misdirected and torn writes that in-block checksums accept.
- **Separate format capability from current policy.** Single-parent namespaces
  are a code choice over a format that could do more; know which of your limits
  are structural and which are policy.
- **One format, one implementation.** A pure core shared by kernel, mkfs, fsck,
  and tests makes the multi-implementation drift that plagues filesystems
  impossible.

That completes the system as it stands: from the firmware's first instruction to
a checksummed, crash-consistent filesystem holding the very programs the kernel
boots. The last two chapters step back — one to the testing philosophy that made
all of it trustworthy, and one to where the road goes from here.
