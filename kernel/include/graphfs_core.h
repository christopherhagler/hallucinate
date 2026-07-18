/*
 * graphfs_core.h - graphfs, the native property-graph filesystem.
 *
 * Everything on disk is a *node* (content + metadata) or a *typed, named
 * edge* between nodes. Edge type NAME is the POSIX namespace: a
 * "directory" is a NAMESPACE node whose outgoing NAME edges are its
 * entries, and path resolution walks NAME edges by name. Other edge types
 * (TAG, REF) share the same storage and become the Phase 6 AI-native
 * substrate — provenance, semantic links — with no format change.
 *
 * ---- Modern on-disk design (v1; docs/book/appendix-k-graphfs.md is authoritative) ----
 *
 * graphfs is copy-on-write and self-checksumming, in the ZFS/APFS mould:
 *
 *  - Nothing live is ever overwritten. A change writes fresh blocks and
 *    is made visible by one atomic superblock write. A power loss either
 *    lands before that write (the change never happened) or after it (the
 *    change is whole) — there is no journal and no repair-on-boot fsck.
 *  - Every metadata block is covered by a crc32c stored in the *pointer*
 *    that reaches it (a self-validating tree); the superblock, having no
 *    parent, checksums itself. Silent media corruption is detected on
 *    read, not served. (Data-block checksums are a documented later
 *    extension, as btrfs shipped: v1 checksums all metadata.)
 *  - Two superblock slots and two allocation-bitmap copies are selected
 *    by `generation & 1`, so a commit never touches the live pair. Mount
 *    takes the valid superblock with the highest generation.
 *  - File content is stored as extents (runs of contiguous blocks), up to
 *    GFS_INLINE_EXTENTS per node; a CoW-friendly allocator keeps freshly
 *    written files to a single extent.
 *
 * The core is pure: plain C over an abstract block-device callback, no
 * kernel dependencies, no dynamic allocation (block scratch lives in
 * struct gfs; the writable allocator and fsck take caller-supplied
 * buffers). It compiles for the kernel, the host mkfs/fsck tools, and the
 * host test suite under ASan/UBSan. One caller at a time per struct gfs.
 *
 * v1 namespace policy (link-time policy, not a format limit): a NAMESPACE
 * node has exactly one incoming NAME edge (single parent → `..` is one
 * field, cycles are impossible); DATA nodes may have any number (hard
 * links). Multi-parent namespaces are a documented later extension.
 *
 * All integers little-endian. Node 1 is the root NAMESPACE node; node 0
 * is reserved as the null id.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#define GFS_BLOCK_SIZE 4096u
#define GFS_SB_MAGIC   0x3153464852475248ull /* "HRGRHFS1", superblock */
#define GFS_EDGE_MAGIC 0x45474447u           /* "GDGE", edge block */
#define GFS_VERSION    1u

#define GFS_NAME_MAX  255u
#define GFS_ROOT_NODE 1u

/* Node table: 256-byte records, 16 per 4 KiB block; node map: one block of
 * 16-byte block pointers → at most 256 table blocks → 4096 node ids. */
#define GFS_NODE_SIZE       ((size_t)256)                    /* bytes per record */
#define GFS_NODES_PER_BLOCK (GFS_BLOCK_SIZE / GFS_NODE_SIZE) /* 16 */
#define GFS_MAP_SLOTS       (GFS_BLOCK_SIZE / 16u)           /* 256 */
#define GFS_MAX_NODES       ((uint64_t)GFS_MAP_SLOTS * GFS_NODES_PER_BLOCK)

/* Extents: 8 inline runs per node (v1 has no extent tree). A run is up to
 * 2^32-1 blocks, so the cap is on fragmentation, not size. */
#define GFS_INLINE_EXTENTS 8u

/* Edge blocks: 14 fixed 272-byte records after a 24-byte header. */
#define GFS_EDGES_PER_BLOCK 14u

/* Node types. */
#define GFS_NODE_FREE      0u
#define GFS_NODE_NAMESPACE 1u
#define GFS_NODE_DATA      2u

/* Edge types. NAME builds the POSIX tree; TAG/REF are reserved for the
 * Phase 6 semantic layer (valid on disk, ignored by path walks). */
#define GFS_EDGE_NAME 1u
#define GFS_EDGE_TAG  2u
#define GFS_EDGE_REF  3u

/* Errors (negative), mapped to POSIX errno by the kernel wrapper. */
enum gfs_err {
    GFS_OK = 0,
    GFS_EIO = -1,           /* device callback failed */
    GFS_EBADFS = -2,        /* superblock/geometry rejected */
    GFS_ECORRUPT = -3,      /* on-disk structure violates the format */
    GFS_EBADCRC = -4,       /* checksum mismatch (corruption detected) */
    GFS_ENOENT = -5,        /* no such node/name */
    GFS_EEXIST = -6,        /* NAME already present in the namespace */
    GFS_ENOTDIR = -7,       /* NAMESPACE node required */
    GFS_EISDIR = -8,        /* DATA node required */
    GFS_ENOSPC = -9,        /* out of blocks or node slots */
    GFS_EINVAL = -10,       /* bad argument (name, offset, type) */
    GFS_ENAMETOOLONG = -11, /* name over GFS_NAME_MAX */
    GFS_ENOTEMPTY = -12,    /* namespace still has NAME edges */
    GFS_EMANYPARENTS = -13, /* second NAME edge onto a namespace */
    GFS_EFBIG = -14,        /* write beyond the addressable size */
    GFS_EROFS = -15,        /* write op on a read-only mount */
    GFS_ENOMEM = -16,       /* caller work buffer too small */
    GFS_EFRAG = -17,        /* file needs more than GFS_INLINE_EXTENTS runs */
    GFS_EWORKSIZE = -18,    /* fsck arena too small */
};

/* Block device callbacks: 4 KiB blocks, return 0 or nonzero on error. */
struct gfs_ops {
    int (*read)(void *ctx, uint64_t lba, void *buf);
    int (*write)(void *ctx, uint64_t lba, const void *buf);
};

/* In-memory node record (decoded from the 256-byte on-disk form). */
struct gfs_node {
    uint32_t type;
    uint32_t mode;
    uint64_t size;
    uint32_t nlink;      /* incoming NAME edges */
    uint32_t edge_count; /* outgoing edges, all types */
    uint64_t parent;     /* NAMESPACE: the single parent node id */
};

/* One decoded edge, produced by gfs_edge_get(). */
struct gfs_edge {
    uint32_t type;
    uint64_t target;
    char name[GFS_NAME_MAX + 1]; /* NUL-terminated */
};

/* A checksummed pointer to a metadata block (the crc lives here, in the
 * parent, not in the block it addresses). */
struct gfs_bp {
    uint64_t phys;
    uint32_t crc;
};

struct gfs {
    const struct gfs_ops *ops;
    void *ctx;
    int writable;

    /* Cached live superblock state. */
    uint64_t generation;
    uint64_t total_blocks;
    uint64_t node_count;
    uint64_t root_node;
    uint64_t bitmap_start;  /* first block of bitmap copy 0 */
    uint64_t bitmap_blocks; /* blocks per copy */
    struct gfs_bp nodemap;  /* pointer to the node-map block */
    uint64_t free_blocks;
    uint64_t free_nodes;

    /* Writable allocator: two bitmaps carved from the caller's mount work
     * buffer. `committed` is the live on-disk allocation state; `working`
     * is the current transaction's view. Allocation draws from blocks free
     * in *both* (so a block freed this transaction — still referenced by
     * the one-generation fallback — is never reused until it commits).
     * NULL for a read-only mount. */
    uint8_t *committed_bm;
    uint8_t *working_bm;
    size_t bm_bytes;

    /* Block scratch (single-caller by contract; struct gfs is heap/PMM
     * allocated, never stack). sb = superblock, map = node map, tbl = a
     * node-table block, a/b = general metadata/data staging. */
    uint8_t sb[GFS_BLOCK_SIZE];
    uint8_t map[GFS_BLOCK_SIZE];
    uint8_t tbl[GFS_BLOCK_SIZE];
    uint8_t a[GFS_BLOCK_SIZE];
    uint8_t b[GFS_BLOCK_SIZE];
};

/* Bytes of mount work buffer a *writable* mount needs (two bitmaps). */
size_t gfs_mount_work_size(uint64_t total_blocks);

/* Bytes of arena gfs_fsck() needs for a given geometry. */
size_t gfs_fsck_work_size(uint64_t node_count, uint64_t total_blocks);

/*
 * Create a fresh filesystem: two superblocks, two bitmap copies, a node
 * map, and a root NAMESPACE node. `node_count` is capped at GFS_MAX_NODES.
 * Fails GFS_EINVAL if the geometry cannot fit.
 */
int gfs_mkfs(const struct gfs_ops *ops, void *ctx, uint64_t total_blocks, uint64_t node_count);

/*
 * Validate a superblock (checksum + geometry) and bind the handle. A
 * writable mount needs `work`/`work_len` for the allocator bitmaps
 * (>= gfs_mount_work_size(total_blocks)); a read-only mount ignores them
 * (pass NULL/0). CoW means the on-disk image is always consistent, so
 * there is no dirty flag and no mount-time repair.
 */
int gfs_mount(struct gfs *fs, const struct gfs_ops *ops, void *ctx, int writable, void *work,
              size_t work_len);

/* Release the handle. State is already durable after each op; this only
 * clears the in-memory binding. */
int gfs_unmount(struct gfs *fs);

/* -------- reading -------- */

int gfs_node_get(struct gfs *fs, uint64_t id, struct gfs_node *out);

/* Find `name` among dir's NAME edges. */
int gfs_lookup(struct gfs *fs, uint64_t dir, const char *name, uint64_t *out);

/* Walk an absolute path ("/bin/init") of plain components; "." and ".."
 * are the VFS's business, not the core's. */
int gfs_resolve(struct gfs *fs, const char *path, uint64_t *out);

/* Decode outgoing edge `index` (0..edge_count-1) of a node. */
int gfs_edge_get(struct gfs *fs, uint64_t id, uint32_t index, struct gfs_edge *out);

/* Read from a DATA node. Returns bytes read (short at EOF) or error. */
long gfs_read(struct gfs *fs, uint64_t id, uint64_t off, void *buf, size_t len);

/* -------- writing (fs must be mounted writable; each call commits) -------- */

/* Allocate a node of `type`. The node is orphaned until linked. */
int gfs_node_create(struct gfs *fs, uint32_t type, uint32_t mode, uint64_t *out);

/*
 * Add an edge dir --name--> target. For NAME edges: dir must be a
 * NAMESPACE node, the name must be new (GFS_EEXIST), and a NAMESPACE
 * target must not already have a parent (GFS_EMANYPARENTS); the target's
 * nlink rises. TAG/REF edges skip the namespace rules.
 */
int gfs_link(struct gfs *fs, uint64_t dir, const char *name, uint64_t target, uint32_t type);

/*
 * Remove the NAME edge dir --name-->. The target's nlink drops; a DATA
 * node reaching nlink 0 is freed with its blocks, a NAMESPACE node must be
 * empty first (GFS_ENOTEMPTY).
 */
int gfs_unlink(struct gfs *fs, uint64_t dir, const char *name);

/* Write to a DATA node, allocating/extending as needed. Returns bytes
 * written or error; never partial on GFS_ENOSPC. */
long gfs_write(struct gfs *fs, uint64_t id, uint64_t off, const void *buf, size_t len);

/*
 * Create a node of `type` and link it as dir --name--> in ONE
 * transaction. The two-call sequence (gfs_node_create + gfs_link) has
 * a crash window that leaves an orphan — which for a NAMESPACE node
 * fsck rightly reports as corruption — so namespace-visible creation
 * (mkdir, O_CREAT) must come through here. Checks as gfs_link
 * (GFS_ENOTDIR, GFS_EEXIST). On success *out is the new node's id.
 */
int gfs_create_at(struct gfs *fs, uint64_t dir, const char *name, uint32_t type, uint32_t mode,
                  uint64_t *out);

/*
 * Atomically move olddir --oldname--> to newdir --newname--> (POSIX
 * rename semantics). An existing newname is replaced: a DATA source
 * replaces a DATA target (else GFS_EISDIR), a NAMESPACE source
 * replaces an *empty* NAMESPACE target (else GFS_ENOTDIR /
 * GFS_ENOTEMPTY); a replaced node whose nlink reaches 0 is freed with
 * its blocks. If oldname and newname already refer to the same node
 * (hard links), nothing happens and the call succeeds. Moving a
 * namespace into itself or its own subtree is GFS_EINVAL.
 */
int gfs_rename(struct gfs *fs, uint64_t olddir, const char *oldname, uint64_t newdir,
               const char *newname);

/*
 * Set a DATA node's size. Shrinking frees whole blocks past the new
 * end and rewrites (CoW) the kept partial last block with its tail
 * zeroed, so a later extension reads zeros — "bytes past EOF in the
 * last block are zero" is a format invariant that fsck verifies.
 * Growing only moves the size: the new range reads as a hole. The
 * tail rewrite can split an extent, so GFS_EFRAG is possible.
 */
int gfs_truncate(struct gfs *fs, uint64_t id, uint64_t size);

/* -------- checking -------- */

/*
 * Full-filesystem verifier over a caller-supplied arena
 * (>= gfs_fsck_work_size(node_count, total_blocks)). Because the format is
 * CoW + checksummed, a healthy image always passes; a failure means a core
 * bug or media corruption. Returns 0 for a clean filesystem or
 * GFS_ECORRUPT after reporting every violation through `report`
 * (printf-shaped line, no trailing newline needed), or GFS_EWORKSIZE if
 * the arena is short.
 */
int gfs_fsck(struct gfs *fs, void *work, size_t work_len,
             void (*report)(void *cookie, const char *msg), void *cookie);

const char *gfs_strerror(int err);
