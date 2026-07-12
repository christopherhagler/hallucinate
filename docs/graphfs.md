# graphfs: The Native Property-Graph Filesystem

graphfs is the from-scratch native filesystem for this OS. It is not a clone
of ext2/FFS: on disk, everything is either a **node** (content + metadata) or
a **typed, named edge** between two nodes. The POSIX namespace is one edge
type layered on that graph — a "directory" is just a node whose outgoing
`NAME` edges are its entries — which leaves `TAG` and `REF` edges as a
first-class, format-stable substrate for the Phase 6 AI layer (provenance,
semantic links) with no on-disk change required.

The design is copy-on-write and self-checksumming, in the ZFS/APFS mould.
This document is **authoritative for the on-disk format (v1)**; the header
[`kernel/include/graphfs_core.h`](../kernel/include/graphfs_core.h) is the
authoritative API.

## Design principles

- **Copy-on-write, never overwrite.** A change writes fresh blocks and is
  made visible by a single atomic superblock write. A power loss either
  lands before that write (the change never happened) or after it (the change
  is whole). There is no journal and no repair-on-boot fsck — the on-disk
  image is *always* structurally consistent.
- **Self-validating tree.** Every metadata block is covered by a crc32c
  stored in the *pointer that reaches it*, not in the block itself (a
  `struct gfs_bp` = `{ phys, crc }`). The superblock, having no parent,
  checksums itself. Silent media corruption is detected on read, not served.
  v1 checksums all metadata; data-block checksums are a documented later
  extension (as btrfs shipped incrementally).
- **Two of everything live-critical.** Two superblock slots and two
  allocation-bitmap copies are selected by `generation & 1`, so a commit
  never writes the currently-live pair. Mount takes the valid superblock with
  the highest generation.
- **A pure core.** [`kernel/fs/graphfs_core.c`](../kernel/fs/graphfs_core.c)
  is plain C over an abstract block-device callback (`struct gfs_ops`), with
  no kernel dependencies and no dynamic allocation — block scratch lives in
  `struct gfs`; the writable allocator and fsck take caller-supplied buffers.
  The identical code compiles into the kernel, the host `mkfs`/`fsck` tools,
  and the ASan/UBSan host test suite. **One caller at a time per `struct
  gfs`** (a VFS sleeping lock will serialize this in 5c).

All integers are little-endian. Node `0` is the reserved null id; node `1`
(`GFS_ROOT_NODE`) is the root `NAMESPACE` node.

## Disk layout

Blocks are 4 KiB (`GFS_BLOCK_SIZE`). A freshly made filesystem lays out a
fixed prefix, then a copy-on-write region that grows on demand:

```
LBA 0            superblock slot 0        (holds even generations)
LBA 1            superblock slot 1        (holds odd generations)
LBA 2            ┐
   ...           ├ allocation bitmap, copy 0   (bitmap_blocks long)
LBA 2+B-1        ┘
LBA 2+B          ┐
   ...           ├ allocation bitmap, copy 1   (bitmap_blocks long)
LBA 2+2B-1       ┘
LBA 2+2B         node-map block           ┐ "data_first": the CoW region.
LBA 2+2B+1       root node-table block     │ mkfs seeds these two; every
   ...           edge blocks, node-table   │ later block is bitmap-allocated
   ...           blocks, file extents ...  ┘ on demand.
```

`B = bitmap_blocks = ceil(ceil(total_blocks/8) / 4096)`. The bitmap covers
*every* block including itself and the superblocks; the fixed-prefix blocks
are marked used at mkfs time. `data_first() = 2 + 2*bitmap_blocks` is the
first allocatable LBA, and the block allocator only ever hands out blocks
at or above it.

### Superblock (`SB_*` offsets, LBA 0 and 1)

Both slots share one format; the live one is `generation & 1`.

| Offset | Size | Field | Notes |
|-------:|-----:|-------|-------|
| 0  | 8 | magic          | `GFS_SB_MAGIC` (`"HRGRHFS1"`) |
| 8  | 4 | version        | `GFS_VERSION` = 1 |
| 12 | 4 | block_size     | 4096 |
| 16 | 4 | crc32c         | over the whole block with this field zeroed |
| 24 | 8 | generation     | monotonic; selects slot and bitmap copy |
| 32 | 8 | total_blocks   | device size in 4 KiB blocks |
| 40 | 8 | node_count     | table capacity, ≤ `GFS_MAX_NODES` (4096) |
| 48 | 8 | root_node      | always `1` |
| 56 | 8 | bitmap_start   | always `2` |
| 64 | 8 | bitmap_blocks  | length of one bitmap copy |
| 72 | 4 | bitmap_crc     | crc32c over the live bitmap copy |
| 80 | 8 | nodemap_phys   | LBA of the node-map block |
| 88 | 4 | nodemap_crc    | crc32c of the node-map block |
| 96 | 8 | free_blocks    | cached accounting |
| 104| 8 | free_nodes     | cached accounting |

Mount reads both slots, keeps the valid one with the highest generation,
then re-checks structural invariants beyond the checksum (`node_count ≥ 2`,
`root_node == 1`, `bitmap_start == 2`, node-map inside the data region, the
whole prefix fitting inside `total_blocks`). Any failure is `GFS_EBADFS`.

### Node map and node table

Nodes are 256-byte records, 16 per block (`GFS_NODES_PER_BLOCK`). The
**node-map** block is one 4 KiB block of 16-byte checksummed pointers
(`{ phys:8, crc:8-as-4 }`, `MP_SIZE` = 16), so at most 256 table blocks →
**4096 node ids** (`GFS_MAX_NODES`). Node `id` lives in table block
`map[id / 16]`, record `id % 16`.

Node record (`ND_*` offsets, 256 bytes):

| Offset | Size | Field | Notes |
|-------:|-----:|-------|-------|
| 0  | 4  | type       | `FREE` (0) / `NAMESPACE` (1) / `DATA` (2) |
| 4  | 4  | mode       | POSIX mode bits |
| 8  | 8  | size       | file length in bytes (DATA) |
| 16 | 4  | nlink      | incoming `NAME` edges |
| 20 | 4  | n_extents  | used inline extent runs |
| 24 | 8  | edge_count | outgoing edges, all types |
| 32 | 8  | parent     | `NAMESPACE`: its single parent node id |
| 40 | 8  | edge_phys  | first edge block (checksummed pointer…) |
| 48 | 4  | edge_crc   | …crc of that edge block |
| 56 | 8×24 | extents  | `GFS_INLINE_EXTENTS` = 8 runs |

An **extent** is `{ logical:8, phys:8, len:8 }` (`EXT_SIZE` = 24) — a run of
`len` contiguous blocks. v1 has no extent tree, so a file is at most 8 runs
(`GFS_EFRAG` past that); each run is up to 2³²−1 blocks, so the practical
cap is fragmentation, not size. The current writable path additionally caps a
single file at `GFS_MAX_FILE_BLOCKS` (512 blocks = 2 MiB).

### Edge blocks

Outgoing edges hang off a node in a singly-linked chain of edge blocks. Each
block is a 24-byte header + 14 fixed 272-byte records (`GFS_EDGES_PER_BLOCK`):

- Header (`EB_*`): `magic` (`"GDGE"`), `count`, and a checksummed
  `{ next_phys, next_crc }` pointer to the continuation block.
- Record (`ER_*`, 272 bytes): `type:4`, `namelen:4`, `target:8`, and a
  `name` of up to `GFS_NAME_MAX` = 255 bytes.

Edge types: `NAME` (1) builds the POSIX tree; `TAG` (2) and `REF` (3) are
valid on disk today but ignored by path resolution — reserved for the Phase 6
semantic layer.

## Namespace policy (v1)

This is a *link-time policy*, not a format limitation:

- A **`NAMESPACE`** node has exactly one incoming `NAME` edge — a single
  parent. So `..` is a single stored field, and directory cycles are
  impossible. A second `NAME` edge onto a namespace is `GFS_EMANYPARENTS`.
- A **`DATA`** node may have any number of incoming `NAME` edges: hard links.
  Unlinking to `nlink == 0` frees the node and its blocks.
- `gfs_unlink` on a non-empty namespace is `GFS_ENOTEMPTY`.

`.` and `..` are the VFS's concern; the core's `gfs_resolve` walks plain path
components only. Multi-parent namespaces are a documented later extension.

## The write transaction

Writable mounts carry two allocator bitmaps carved from the caller's work
buffer (`gfs_mount_work_size` = `2 * ceil(total_blocks/8)` bytes):

- **`committed_bm`** — the live on-disk allocation state.
- **`working_bm`** — the current transaction's view.

Allocation draws only from blocks free in **both** bitmaps, so a block freed
this transaction — still reachable through the one-generation fallback
superblock — is never reused until the transaction commits. Each mutating
call (`gfs_node_create`, `gfs_link`, `gfs_unlink`, `gfs_write`) is its own
transaction: it CoWs the metadata it touches up to the node map, writes the
inactive bitmap copy and inactive superblock slot at `generation + 1`, and
returns only once that superblock write lands. There is no partial write on
`GFS_ENOSPC`.

## Tools and testing

Because the core is pure, the host tools are thin drivers over the exact code
the kernel mounts — the format has a single implementation:

- [`tools/graphfs_mkfs.c`](../tools/graphfs_mkfs.c) — `graphfs_mkfs --out
  img --size-mib N [/path=host_file ...]` makes a filesystem and installs
  host files at absolute paths, creating intermediate namespaces. The build
  uses it to lay `/bin/init` and `/bin/hello` onto `build/fs.img`.
- [`tools/graphfs_fsck.c`](../tools/graphfs_fsck.c) — `graphfs_fsck img`
  runs `gfs_fsck` over the image. Since a healthy CoW+checksummed image
  always passes, a failure means a core bug or media corruption. `make
  check-fsck` gates the freshly built image; the 5d crash-consistency gate
  will run the same check after boot.

Three test levels cover the format:

- **Host unit tests** — [`tests/host/test_graphfs.c`](../tests/host/test_graphfs.c),
  built into `make check-host` under ASan/UBSan against an in-memory device.
- **fsck gate** — `make check-fsck`, above.
- **Boot integration** — the kernel mounts `build/fs.img` over virtio-blk
  during `make check-boot` (read path in 5c).

## v1 limits (carried, by design)

- Metadata-only checksums (no data-block checksums yet).
- 8 inline extents per file, 512-block (2 MiB) max file, 4096 nodes.
- Single-parent namespaces; one caller at a time per handle.
- No extent tree, no snapshots surfaced, no compression.

Each is a documented future extension, not a shortcut: the CoW +
checksummed-pointer format was chosen so these can be added without breaking
the on-disk layout.
