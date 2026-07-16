/*
 * graphfs_core.c - the copy-on-write, self-checksumming graphfs core.
 *
 * Pure C over the struct gfs_ops block callbacks (see graphfs_core.h and
 * docs/book/appendix-k-graphfs.md for the model and on-disk format). The whole module is
 * host-testable under ASan/UBSan; the kernel driver, the mkfs tool, and
 * the fsck tool are thin wrappers over exactly this code.
 *
 * Every mutating call is one transaction: it writes fresh blocks into free
 * space, then makes them live with a single superblock write. Any error
 * before that write leaves the committed tree untouched, so operations are
 * all-or-nothing and a crash is always recoverable to the last commit.
 */
#include <graphfs_core.h>

#include <crc32c.h>
#include <fmt.h>
#include <string.h>

/* ---- on-disk layout (all little-endian) -------------------------------- */

/* Superblock (self-checksummed; two slots at block 0 and 1). */
#define SB_MAGIC        0
#define SB_VERSION      8
#define SB_BLOCK_SIZE   12
#define SB_CRC          16 /* over the block with this field zeroed */
#define SB_GEN          24
#define SB_TOTAL        32
#define SB_NODE_COUNT   40
#define SB_ROOT         48
#define SB_BM_START     56
#define SB_BM_BLOCKS    64
#define SB_BM_CRC       72
#define SB_NODEMAP_PHYS 80
#define SB_NODEMAP_CRC  88
#define SB_FREE_BLOCKS  96
#define SB_FREE_NODES   104

/* Node record (256 bytes, 16 per table block). */
#define ND_TYPE      0
#define ND_MODE      4
#define ND_SIZE      8
#define ND_NLINK     16
#define ND_NEXTENTS  20
#define ND_EDGECOUNT 24
#define ND_PARENT    32
#define ND_EDGE_PHYS 40
#define ND_EDGE_CRC  48
#define ND_EXTENTS   56 /* GFS_INLINE_EXTENTS * EXT_SIZE */
#define EXT_SIZE     ((size_t)24)
#define EXT_LOGICAL  0
#define EXT_PHYS     8
#define EXT_LEN      16

/* Node-map entry (16 bytes). */
#define MP_PHYS 0
#define MP_CRC  8
#define MP_SIZE 16

/* Edge block header + fixed 272-byte records. */
#define EB_MAGIC     0
#define EB_COUNT     4
#define EB_NEXT_PHYS 8
#define EB_NEXT_CRC  16
#define EB_RECORDS   24
#define ER_SIZE      ((size_t)272)
#define ER_TYPE      0
#define ER_NAMELEN   4
#define ER_TARGET    8
#define ER_NAME      16

/* A single gfs_write touches at most this many blocks (2 MiB); larger
 * files are a later extent-tree extension. Bounds the phys map in fs->a. */
#define GFS_MAX_FILE_BLOCKS 512u
#define GFS_MAX_FILE_SIZE   ((uint64_t)GFS_MAX_FILE_BLOCKS * GFS_BLOCK_SIZE)

/* Allocation-bitmap bits addressable by one 4 KiB block. */
#define BM_BITS_PER_BLOCK ((uint64_t)GFS_BLOCK_SIZE * 8)

/* ---- little-endian scalars --------------------------------------------- */

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t rd64(const uint8_t *p) {
    return (uint64_t)rd32(p) | ((uint64_t)rd32(p + 4) << 32);
}
static void wr32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}
static void wr64(uint8_t *p, uint64_t v) {
    wr32(p, (uint32_t)v);
    wr32(p + 4, (uint32_t)(v >> 32));
}

/* ---- internal node form (full record, incl. extents) ------------------- */

struct gfs_extent {
    uint64_t logical;
    uint64_t phys;
    uint32_t len;
};

struct disk_node {
    uint32_t type;
    uint32_t mode;
    uint64_t size;
    uint32_t nlink;
    uint32_t n_extents;
    uint32_t edge_count;
    uint64_t parent;
    struct gfs_bp edge_head;
    struct gfs_extent ext[GFS_INLINE_EXTENTS];
};

static void node_decode(const uint8_t *r, struct disk_node *n) {
    n->type = rd32(r + ND_TYPE);
    n->mode = rd32(r + ND_MODE);
    n->size = rd64(r + ND_SIZE);
    n->nlink = rd32(r + ND_NLINK);
    n->n_extents = rd32(r + ND_NEXTENTS);
    n->edge_count = rd32(r + ND_EDGECOUNT);
    n->parent = rd64(r + ND_PARENT);
    n->edge_head.phys = rd64(r + ND_EDGE_PHYS);
    n->edge_head.crc = rd32(r + ND_EDGE_CRC);
    for (uint32_t k = 0; k < GFS_INLINE_EXTENTS; k++) {
        const uint8_t *e = r + ND_EXTENTS + (k * EXT_SIZE);
        n->ext[k].logical = rd64(e + EXT_LOGICAL);
        n->ext[k].phys = rd64(e + EXT_PHYS);
        n->ext[k].len = rd32(e + EXT_LEN);
    }
}

static void node_encode(uint8_t *r, const struct disk_node *n) {
    memset(r, 0, GFS_NODE_SIZE);
    wr32(r + ND_TYPE, n->type);
    wr32(r + ND_MODE, n->mode);
    wr64(r + ND_SIZE, n->size);
    wr32(r + ND_NLINK, n->nlink);
    wr32(r + ND_NEXTENTS, n->n_extents);
    wr32(r + ND_EDGECOUNT, n->edge_count);
    wr64(r + ND_PARENT, n->parent);
    wr64(r + ND_EDGE_PHYS, n->edge_head.phys);
    wr32(r + ND_EDGE_CRC, n->edge_head.crc);
    for (uint32_t k = 0; k < GFS_INLINE_EXTENTS; k++) {
        uint8_t *e = r + ND_EXTENTS + (k * EXT_SIZE);
        wr64(e + EXT_LOGICAL, n->ext[k].logical);
        wr64(e + EXT_PHYS, n->ext[k].phys);
        wr32(e + EXT_LEN, n->ext[k].len);
    }
}

/* ---- raw + checksummed block I/O --------------------------------------- */

static int bread(struct gfs *fs, uint64_t lba, void *buf) {
    if (lba >= fs->total_blocks) {
        return GFS_ECORRUPT;
    }
    return fs->ops->read(fs->ctx, lba, buf) ? GFS_EIO : GFS_OK;
}

static int bwrite(struct gfs *fs, uint64_t lba, const void *buf) {
    if (lba >= fs->total_blocks) {
        return GFS_ECORRUPT;
    }
    return fs->ops->write(fs->ctx, lba, buf) ? GFS_EIO : GFS_OK;
}

/* Read a metadata block and verify it against the crc in its parent bp. */
static int read_meta(struct gfs *fs, struct gfs_bp bp, void *buf) {
    int rc = bread(fs, bp.phys, buf);
    if (rc != GFS_OK) {
        return rc;
    }
    if (crc32c(buf, GFS_BLOCK_SIZE) != bp.crc) {
        return GFS_EBADCRC;
    }
    return GFS_OK;
}

/* Write a metadata block and return the pointer (phys + fresh crc). */
static int write_meta(struct gfs *fs, uint64_t phys, const void *buf, struct gfs_bp *out) {
    int rc = bwrite(fs, phys, buf);
    if (rc != GFS_OK) {
        return rc;
    }
    out->phys = phys;
    out->crc = crc32c(buf, GFS_BLOCK_SIZE);
    return GFS_OK;
}

/* ---- allocation bitmap (writable mounts) ------------------------------- */

static int bit_test(const uint8_t *bm, uint64_t i) {
    return (bm[i >> 3] >> (i & 7)) & 1;
}
static void bit_set(uint8_t *bm, uint64_t i) {
    bm[i >> 3] |= (uint8_t)(1u << (i & 7));
}
static void bit_clr(uint8_t *bm, uint64_t i) {
    bm[i >> 3] &= (uint8_t)~(1u << (i & 7));
}

static uint64_t data_first(const struct gfs *fs) {
    return fs->bitmap_start + (2 * fs->bitmap_blocks);
}

/* Allocate a block free in both the committed and working bitmaps, so a
 * block still reachable from the one-generation fallback is never reused
 * mid-transaction. */
static int alloc_block(struct gfs *fs, uint64_t *out) {
    for (uint64_t i = data_first(fs); i < fs->total_blocks; i++) {
        if (!bit_test(fs->committed_bm, i) && !bit_test(fs->working_bm, i)) {
            bit_set(fs->working_bm, i);
            *out = i;
            return GFS_OK;
        }
    }
    return GFS_ENOSPC;
}

/* Prefer `hint` (keeps sequential writes to one extent), else scan. */
static int alloc_near(struct gfs *fs, uint64_t hint, uint64_t *out) {
    if (hint >= data_first(fs) && hint < fs->total_blocks && !bit_test(fs->committed_bm, hint) &&
        !bit_test(fs->working_bm, hint)) {
        bit_set(fs->working_bm, hint);
        *out = hint;
        return GFS_OK;
    }
    return alloc_block(fs, out);
}

static void free_block(struct gfs *fs, uint64_t blk) {
    if (blk != 0) {
        bit_clr(fs->working_bm, blk);
    }
}

static uint64_t count_free(const struct gfs *fs) {
    uint64_t c = 0;
    for (uint64_t i = 0; i < fs->total_blocks; i++) {
        if (!bit_test(fs->working_bm, i)) {
            c++;
        }
    }
    return c;
}

/* ---- node map + node records ------------------------------------------- */

static struct gfs_bp map_get(const struct gfs *fs, uint64_t bi) {
    const uint8_t *e = fs->map + (bi * MP_SIZE);
    struct gfs_bp bp = {rd64(e + MP_PHYS), rd32(e + MP_CRC)};
    return bp;
}

static void map_set(struct gfs *fs, uint64_t bi, struct gfs_bp bp) {
    uint8_t *e = fs->map + (bi * MP_SIZE);
    wr64(e + MP_PHYS, bp.phys);
    wr32(e + MP_CRC, bp.crc);
    wr32(e + MP_CRC + 4, 0);
}

/* Read node `id` through the working map into `n` (a phys-0 map slot means
 * the node is FREE). Uses fs->tbl. */
static int node_read(struct gfs *fs, uint64_t id, struct disk_node *n) {
    if (id == 0 || id >= fs->node_count) {
        return GFS_EINVAL;
    }
    struct gfs_bp bp = map_get(fs, id / GFS_NODES_PER_BLOCK);
    if (bp.phys == 0) {
        memset(n, 0, sizeof(*n));
        return GFS_OK;
    }
    int rc = read_meta(fs, bp, fs->tbl);
    if (rc != GFS_OK) {
        return rc;
    }
    node_decode(fs->tbl + ((id % GFS_NODES_PER_BLOCK) * GFS_NODE_SIZE), n);
    return GFS_OK;
}

/* Copy-on-write a node record: rewrite its table block to a fresh block and
 * point the working map at it. Uses fs->tbl. */
static int node_write(struct gfs *fs, uint64_t id, const struct disk_node *n) {
    uint64_t bi = id / GFS_NODES_PER_BLOCK;
    struct gfs_bp old = map_get(fs, bi);
    if (old.phys == 0) {
        memset(fs->tbl, 0, GFS_BLOCK_SIZE);
    } else {
        int rc = read_meta(fs, old, fs->tbl);
        if (rc != GFS_OK) {
            return rc;
        }
    }
    node_encode(fs->tbl + ((id % GFS_NODES_PER_BLOCK) * GFS_NODE_SIZE), n);

    uint64_t phys;
    int rc = alloc_block(fs, &phys);
    if (rc != GFS_OK) {
        return rc;
    }
    struct gfs_bp bp;
    rc = write_meta(fs, phys, fs->tbl, &bp);
    if (rc != GFS_OK) {
        return rc;
    }
    map_set(fs, bi, bp);
    free_block(fs, old.phys);
    return GFS_OK;
}

/* ---- transactions ------------------------------------------------------ */

/* Start a transaction: reset the working bitmap and load the live node map
 * into fs->map (all node access this transaction goes through it). */
static int txn_begin(struct gfs *fs) {
    if (!fs->writable) {
        return GFS_EROFS;
    }
    memcpy(fs->working_bm, fs->committed_bm, fs->bm_bytes);
    return read_meta(fs, fs->nodemap, fs->map);
}

static uint32_t bitmap_crc(const struct gfs *fs) {
    return crc32c(fs->working_bm, fs->bm_bytes);
}

/* Write bitmap copy `parity` from a bitmap buffer, block by block. */
static int write_bitmap(struct gfs *fs, uint64_t parity, const uint8_t *bm) {
    for (uint64_t j = 0; j < fs->bitmap_blocks; j++) {
        memset(fs->b, 0, GFS_BLOCK_SIZE);
        uint64_t off = j * GFS_BLOCK_SIZE;
        size_t chunk = fs->bm_bytes - off < GFS_BLOCK_SIZE ? fs->bm_bytes - off : GFS_BLOCK_SIZE;
        memcpy(fs->b, bm + off, chunk);
        int rc = bwrite(fs, fs->bitmap_start + (parity * fs->bitmap_blocks) + j, fs->b);
        if (rc != GFS_OK) {
            return rc;
        }
    }
    return GFS_OK;
}

static void sb_encode(struct gfs *fs, uint64_t gen, struct gfs_bp nodemap, uint32_t bm_crc,
                      uint64_t free_blocks, uint64_t free_nodes) {
    uint8_t *s = fs->sb;
    memset(s, 0, GFS_BLOCK_SIZE);
    wr64(s + SB_MAGIC, GFS_SB_MAGIC);
    wr32(s + SB_VERSION, GFS_VERSION);
    wr32(s + SB_BLOCK_SIZE, GFS_BLOCK_SIZE);
    wr64(s + SB_GEN, gen);
    wr64(s + SB_TOTAL, fs->total_blocks);
    wr64(s + SB_NODE_COUNT, fs->node_count);
    wr64(s + SB_ROOT, fs->root_node);
    wr64(s + SB_BM_START, fs->bitmap_start);
    wr64(s + SB_BM_BLOCKS, fs->bitmap_blocks);
    wr32(s + SB_BM_CRC, bm_crc);
    wr64(s + SB_NODEMAP_PHYS, nodemap.phys);
    wr32(s + SB_NODEMAP_CRC, nodemap.crc);
    wr64(s + SB_FREE_BLOCKS, free_blocks);
    wr64(s + SB_FREE_NODES, free_nodes);
    wr32(s + SB_CRC, 0);
    wr32(s + SB_CRC, crc32c(s, GFS_BLOCK_SIZE));
}

/*
 * Commit the transaction. Order is chosen so a crash at any point falls
 * back cleanly to the previous generation: new data/metadata blocks are
 * already on disk; here we write the new node map, the inactive bitmap
 * copy, and finally the inactive superblock slot — the single atomic
 * publish. `nodes_delta` adjusts the free-node count (allocated < 0).
 */
static int txn_commit(struct gfs *fs, long nodes_delta) {
    uint64_t new_gen = fs->generation + 1;
    uint64_t parity = new_gen & 1;

    /* New node map from fs->map. */
    uint64_t mp_phys;
    int rc = alloc_block(fs, &mp_phys);
    if (rc != GFS_OK) {
        return rc;
    }
    struct gfs_bp new_map;
    rc = write_meta(fs, mp_phys, fs->map, &new_map);
    if (rc != GFS_OK) {
        return rc;
    }
    free_block(fs, fs->nodemap.phys);

    uint64_t free_blocks = count_free(fs);
    uint64_t free_nodes = fs->free_nodes;
    if (nodes_delta >= 0) {
        free_nodes += (uint64_t)nodes_delta;
    } else {
        free_nodes -= (uint64_t)(-nodes_delta);
    }

    rc = write_bitmap(fs, parity, fs->working_bm);
    if (rc != GFS_OK) {
        return rc;
    }
    uint32_t bm_crc = bitmap_crc(fs);

    sb_encode(fs, new_gen, new_map, bm_crc, free_blocks, free_nodes);
    rc = bwrite(fs, parity, fs->sb); /* the atomic publish */
    if (rc != GFS_OK) {
        return rc;
    }

    /* Adopt the committed state. */
    fs->generation = new_gen;
    fs->nodemap = new_map;
    fs->free_blocks = free_blocks;
    fs->free_nodes = free_nodes;
    memcpy(fs->committed_bm, fs->working_bm, fs->bm_bytes);
    return GFS_OK;
}

/* ---- edges ------------------------------------------------------------- */

static void edge_encode(uint8_t *rec, uint32_t type, const char *name, uint32_t namelen,
                        uint64_t target) {
    memset(rec, 0, ER_SIZE);
    wr32(rec + ER_TYPE, type);
    wr32(rec + ER_NAMELEN, namelen);
    wr64(rec + ER_TARGET, target);
    memcpy(rec + ER_NAME, name, namelen);
}

/* Add an edge by prepending to the node's edge chain: O(1), CoW-friendly
 * (only the head block and the node change). Uses fs->a. */
static int edge_add(struct gfs *fs, struct disk_node *node, uint32_t type, uint64_t target,
                    const char *name, uint32_t namelen) {
    struct gfs_bp head = node->edge_head;
    if (head.phys != 0) {
        int rc = read_meta(fs, head, fs->a);
        if (rc != GFS_OK) {
            return rc;
        }
        uint32_t count = rd32(fs->a + EB_COUNT);
        if (count < GFS_EDGES_PER_BLOCK) {
            edge_encode(fs->a + EB_RECORDS + (count * ER_SIZE), type, name, namelen, target);
            wr32(fs->a + EB_COUNT, count + 1);
            uint64_t phys;
            rc = alloc_block(fs, &phys);
            if (rc != GFS_OK) {
                return rc;
            }
            struct gfs_bp bp;
            rc = write_meta(fs, phys, fs->a, &bp);
            if (rc != GFS_OK) {
                return rc;
            }
            free_block(fs, head.phys);
            node->edge_head = bp;
            node->edge_count++;
            return GFS_OK;
        }
    }
    /* Fresh head block chaining to the old head. */
    memset(fs->a, 0, GFS_BLOCK_SIZE);
    wr32(fs->a + EB_MAGIC, GFS_EDGE_MAGIC);
    wr32(fs->a + EB_COUNT, 1);
    wr64(fs->a + EB_NEXT_PHYS, head.phys);
    wr32(fs->a + EB_NEXT_CRC, head.crc);
    edge_encode(fs->a + EB_RECORDS, type, name, namelen, target);
    uint64_t phys;
    int rc = alloc_block(fs, &phys);
    if (rc != GFS_OK) {
        return rc;
    }
    struct gfs_bp bp;
    rc = write_meta(fs, phys, fs->a, &bp);
    if (rc != GFS_OK) {
        return rc;
    }
    node->edge_head = bp;
    node->edge_count++;
    return GFS_OK;
}

/* Remove the NAME edge `name` by rebuilding the whole chain into fresh
 * blocks (fine for v1 directory sizes; a b-tree is future work). Reads old
 * blocks via fs->a, builds the new chain in fs->b. */
static int edge_remove_name(struct gfs *fs, struct disk_node *node, const char *name,
                            uint32_t namelen, uint64_t *removed_target) {
    struct gfs_bp cur = node->edge_head;
    struct gfs_bp out_prev = {0, 0};
    uint32_t out_count = 0;
    uint32_t total = 0;
    int found = 0;

    memset(fs->b, 0, GFS_BLOCK_SIZE);
    wr32(fs->b + EB_MAGIC, GFS_EDGE_MAGIC);

    while (cur.phys != 0) {
        int rc = read_meta(fs, cur, fs->a);
        if (rc != GFS_OK) {
            return rc;
        }
        uint32_t count = rd32(fs->a + EB_COUNT);
        struct gfs_bp next = {rd64(fs->a + EB_NEXT_PHYS), rd32(fs->a + EB_NEXT_CRC)};
        for (uint32_t k = 0; k < count; k++) {
            uint8_t *rec = fs->a + EB_RECORDS + (k * ER_SIZE);
            if (!found && rd32(rec + ER_TYPE) == GFS_EDGE_NAME &&
                rd32(rec + ER_NAMELEN) == namelen && memcmp(rec + ER_NAME, name, namelen) == 0) {
                found = 1;
                *removed_target = rd64(rec + ER_TARGET);
                continue;
            }
            if (out_count == GFS_EDGES_PER_BLOCK) {
                wr32(fs->b + EB_COUNT, out_count);
                wr64(fs->b + EB_NEXT_PHYS, out_prev.phys);
                wr32(fs->b + EB_NEXT_CRC, out_prev.crc);
                uint64_t phys;
                rc = alloc_block(fs, &phys);
                if (rc != GFS_OK) {
                    return rc;
                }
                rc = write_meta(fs, phys, fs->b, &out_prev);
                if (rc != GFS_OK) {
                    return rc;
                }
                out_count = 0;
                memset(fs->b, 0, GFS_BLOCK_SIZE);
                wr32(fs->b + EB_MAGIC, GFS_EDGE_MAGIC);
            }
            memcpy(fs->b + EB_RECORDS + (out_count * ER_SIZE), rec, ER_SIZE);
            out_count++;
            total++;
        }
        free_block(fs, cur.phys);
        cur = next;
    }

    if (!found) {
        return GFS_ENOENT;
    }

    struct gfs_bp new_head = out_prev;
    if (out_count > 0) {
        wr32(fs->b + EB_COUNT, out_count);
        wr64(fs->b + EB_NEXT_PHYS, out_prev.phys);
        wr32(fs->b + EB_NEXT_CRC, out_prev.crc);
        uint64_t phys;
        int rc = alloc_block(fs, &phys);
        if (rc != GFS_OK) {
            return rc;
        }
        rc = write_meta(fs, phys, fs->b, &new_head);
        if (rc != GFS_OK) {
            return rc;
        }
    }
    node->edge_head = new_head;
    node->edge_count = total;
    return GFS_OK;
}

/* Free every block of a node's edge chain (used when the node is freed). */
static int edge_chain_free(struct gfs *fs, struct gfs_bp head) {
    while (head.phys != 0) {
        int rc = read_meta(fs, head, fs->a);
        if (rc != GFS_OK) {
            return rc;
        }
        struct gfs_bp next = {rd64(fs->a + EB_NEXT_PHYS), rd32(fs->a + EB_NEXT_CRC)};
        free_block(fs, head.phys);
        head = next;
    }
    return GFS_OK;
}

/* Find `name` among dir's NAME edges without a transaction context. Uses
 * fs->a (edge blocks) and fs->tbl (via node_read for the caller). */
static int edge_lookup(struct gfs *fs, const struct disk_node *dir, const char *name,
                       uint32_t namelen, uint64_t *out) {
    struct gfs_bp cur = dir->edge_head;
    while (cur.phys != 0) {
        int rc = read_meta(fs, cur, fs->a);
        if (rc != GFS_OK) {
            return rc;
        }
        uint32_t count = rd32(fs->a + EB_COUNT);
        struct gfs_bp next = {rd64(fs->a + EB_NEXT_PHYS), rd32(fs->a + EB_NEXT_CRC)};
        for (uint32_t k = 0; k < count; k++) {
            const uint8_t *rec = fs->a + EB_RECORDS + (k * ER_SIZE);
            if (rd32(rec + ER_TYPE) == GFS_EDGE_NAME && rd32(rec + ER_NAMELEN) == namelen &&
                memcmp(rec + ER_NAME, name, namelen) == 0) {
                *out = rd64(rec + ER_TARGET);
                return GFS_OK;
            }
        }
        cur = next;
    }
    return GFS_ENOENT;
}

/* ---- superblock validation / mount ------------------------------------- */

static int sb_valid(const uint8_t *s, uint64_t *gen_out) {
    if (rd64(s + SB_MAGIC) != GFS_SB_MAGIC || rd32(s + SB_VERSION) != GFS_VERSION ||
        rd32(s + SB_BLOCK_SIZE) != GFS_BLOCK_SIZE) {
        return 0;
    }
    uint32_t stored = rd32(s + SB_CRC);
    uint8_t tmp[GFS_BLOCK_SIZE];
    memcpy(tmp, s, GFS_BLOCK_SIZE);
    wr32(tmp + SB_CRC, 0);
    if (crc32c(tmp, GFS_BLOCK_SIZE) != stored) {
        return 0;
    }
    *gen_out = rd64(s + SB_GEN);
    return 1;
}

static void sb_decode(struct gfs *fs, const uint8_t *s) {
    fs->generation = rd64(s + SB_GEN);
    fs->total_blocks = rd64(s + SB_TOTAL);
    fs->node_count = rd64(s + SB_NODE_COUNT);
    fs->root_node = rd64(s + SB_ROOT);
    fs->bitmap_start = rd64(s + SB_BM_START);
    fs->bitmap_blocks = rd64(s + SB_BM_BLOCKS);
    fs->nodemap.phys = rd64(s + SB_NODEMAP_PHYS);
    fs->nodemap.crc = rd32(s + SB_NODEMAP_CRC);
    fs->free_blocks = rd64(s + SB_FREE_BLOCKS);
    fs->free_nodes = rd64(s + SB_FREE_NODES);
}

size_t gfs_mount_work_size(uint64_t total_blocks) {
    return 2 * (size_t)((total_blocks + 7) / 8);
}

/* Geometry sanity beyond the checksum: the format's structural invariants. */
static int sb_geometry_ok(const struct gfs *fs) {
    if (fs->node_count < 2 || fs->node_count > GFS_MAX_NODES) {
        return 0;
    }
    if (fs->root_node != GFS_ROOT_NODE) {
        return 0;
    }
    if (fs->bitmap_start != 2 || fs->bitmap_blocks == 0) {
        return 0;
    }
    if (data_first(fs) > fs->total_blocks) {
        return 0;
    }
    if (fs->nodemap.phys < data_first(fs) || fs->nodemap.phys >= fs->total_blocks) {
        return 0;
    }
    return 1;
}

static int read_bitmap(struct gfs *fs, uint64_t parity, uint8_t *bm) {
    for (uint64_t j = 0; j < fs->bitmap_blocks; j++) {
        int rc = bread(fs, fs->bitmap_start + (parity * fs->bitmap_blocks) + j, fs->b);
        if (rc != GFS_OK) {
            return rc;
        }
        uint64_t off = j * GFS_BLOCK_SIZE;
        size_t chunk = fs->bm_bytes - off < GFS_BLOCK_SIZE ? fs->bm_bytes - off : GFS_BLOCK_SIZE;
        memcpy(bm + off, fs->b, chunk);
    }
    return GFS_OK;
}

int gfs_mount(struct gfs *fs, const struct gfs_ops *ops, void *ctx, int writable, void *work,
              size_t work_len) {
    memset(fs, 0, sizeof(*fs));
    fs->ops = ops;
    fs->ctx = ctx;

    uint8_t chosen[GFS_BLOCK_SIZE];
    uint64_t best_gen = 0;
    int have = 0;
    for (uint64_t slot = 0; slot < 2; slot++) {
        if (ops->read(ctx, slot, fs->sb) != 0) {
            continue;
        }
        uint64_t gen;
        if (sb_valid(fs->sb, &gen) && (!have || gen > best_gen)) {
            best_gen = gen;
            memcpy(chosen, fs->sb, GFS_BLOCK_SIZE);
            have = 1;
        }
    }
    if (!have) {
        return GFS_EBADFS;
    }
    sb_decode(fs, chosen);
    if (!sb_geometry_ok(fs)) {
        return GFS_EBADFS;
    }
    /* Validate the tree root (also warms fs->map). */
    int rc = read_meta(fs, fs->nodemap, fs->map);
    if (rc != GFS_OK) {
        return rc == GFS_EBADCRC ? GFS_EBADCRC : GFS_EBADFS;
    }

    fs->writable = writable;
    if (writable) {
        if (!work || work_len < gfs_mount_work_size(fs->total_blocks)) {
            return GFS_ENOMEM;
        }
        fs->bm_bytes = (size_t)((fs->total_blocks + 7) / 8);
        fs->committed_bm = (uint8_t *)work;
        fs->working_bm = (uint8_t *)work + fs->bm_bytes;
        rc = read_bitmap(fs, fs->generation & 1, fs->committed_bm);
        if (rc != GFS_OK) {
            return rc;
        }
        if (crc32c(fs->committed_bm, fs->bm_bytes) != rd32(chosen + SB_BM_CRC)) {
            return GFS_EBADCRC;
        }
        memcpy(fs->working_bm, fs->committed_bm, fs->bm_bytes);
    }
    return GFS_OK;
}

int gfs_unmount(struct gfs *fs) {
    fs->ops = NULL;
    fs->ctx = NULL;
    return GFS_OK;
}

/* ---- mkfs -------------------------------------------------------------- */

int gfs_mkfs(const struct gfs_ops *ops, void *ctx, uint64_t total_blocks, uint64_t node_count) {
    if (node_count < 2) {
        node_count = 2;
    }
    if (node_count > GFS_MAX_NODES) {
        node_count = GFS_MAX_NODES;
    }
    uint64_t bm_bytes = (total_blocks + 7) / 8;
    uint64_t bitmap_blocks = (bm_bytes + GFS_BLOCK_SIZE - 1) / GFS_BLOCK_SIZE;
    if (bitmap_blocks == 0) {
        bitmap_blocks = 1;
    }
    uint64_t bitmap_start = 2;
    uint64_t nodemap_phys = bitmap_start + (2 * bitmap_blocks);
    uint64_t root_tbl_phys = nodemap_phys + 1;
    uint64_t used_prefix = root_tbl_phys + 1; /* first free block */
    if (used_prefix > total_blocks) {
        return GFS_EINVAL;
    }

    uint8_t blk[GFS_BLOCK_SIZE];

    /* Root node table block: id 0 reserved FREE, id 1 the root NAMESPACE. */
    memset(blk, 0, GFS_BLOCK_SIZE);
    struct disk_node root = {0};
    root.type = GFS_NODE_NAMESPACE;
    root.mode = 0;
    root.parent = GFS_ROOT_NODE; /* "/.." is "/" */
    node_encode(blk + (GFS_ROOT_NODE * GFS_NODE_SIZE), &root);
    uint32_t root_tbl_crc = crc32c(blk, GFS_BLOCK_SIZE);
    if (ops->write(ctx, root_tbl_phys, blk) != 0) {
        return GFS_EIO;
    }

    /* Node map: slot 0 → root table block, rest empty. */
    memset(blk, 0, GFS_BLOCK_SIZE);
    wr64(blk + MP_PHYS, root_tbl_phys);
    wr32(blk + MP_CRC, root_tbl_crc);
    uint32_t nodemap_crc = crc32c(blk, GFS_BLOCK_SIZE);
    if (ops->write(ctx, nodemap_phys, blk) != 0) {
        return GFS_EIO;
    }

    /* Both bitmap copies: blocks [0, used_prefix) used, plus padding bits
     * for any block index >= total_blocks (so they are never allocated). */
    uint32_t bmcrc = 0;
    for (uint64_t copy = 0; copy < 2; copy++) {
        uint32_t running = CRC32C_INIT;
        for (uint64_t j = 0; j < bitmap_blocks; j++) {
            memset(blk, 0, GFS_BLOCK_SIZE);
            uint64_t base = j * BM_BITS_PER_BLOCK;
            for (uint64_t bit = 0; bit < BM_BITS_PER_BLOCK; bit++) {
                uint64_t b = base + bit;
                if (b >= total_blocks) {
                    break;
                }
                if (b < used_prefix) {
                    blk[bit >> 3] |= (uint8_t)(1u << (bit & 7));
                }
            }
            if (ops->write(ctx, bitmap_start + (copy * bitmap_blocks) + j, blk) != 0) {
                return GFS_EIO;
            }
            uint64_t off = j * GFS_BLOCK_SIZE;
            size_t chunk =
                bm_bytes - off < GFS_BLOCK_SIZE ? (size_t)(bm_bytes - off) : GFS_BLOCK_SIZE;
            running = crc32c_update(running, blk, chunk);
        }
        bmcrc = crc32c_final(running);
    }

    /* Trailing padding bits within bm_bytes (blocks total_blocks..) must be
     * set for the crc computed above to match a later mounted bitmap; they
     * already are, because the loop only sets bits < used_prefix and the
     * mounted allocator treats out-of-range bits as used via the same
     * mkfs-written bytes. */

    uint64_t free_blocks = total_blocks - used_prefix;
    uint64_t free_nodes = node_count - 2; /* ids 1..node_count-1, minus root */

    /* Two superblocks: gen 0 in slot 0, gen 1 in slot 1 (live), both naming
     * the same freshly built tree. First real commit becomes gen 2. */
    struct gfs tmp;
    memset(&tmp, 0, sizeof(tmp));
    tmp.total_blocks = total_blocks;
    tmp.node_count = node_count;
    tmp.root_node = GFS_ROOT_NODE;
    tmp.bitmap_start = bitmap_start;
    tmp.bitmap_blocks = bitmap_blocks;
    struct gfs_bp nm = {nodemap_phys, nodemap_crc};
    for (uint64_t gen = 0; gen < 2; gen++) {
        sb_encode(&tmp, gen, nm, bmcrc, free_blocks, free_nodes);
        if (ops->write(ctx, gen, tmp.sb) != 0) {
            return GFS_EIO;
        }
    }
    return GFS_OK;
}

/* ---- reading ----------------------------------------------------------- */

/* Read-only ops load the live node map into fs->map first. */
static int map_load(struct gfs *fs) {
    return read_meta(fs, fs->nodemap, fs->map);
}

int gfs_node_get(struct gfs *fs, uint64_t id, struct gfs_node *out) {
    int rc = map_load(fs);
    if (rc != GFS_OK) {
        return rc;
    }
    struct disk_node n;
    rc = node_read(fs, id, &n);
    if (rc != GFS_OK) {
        return rc;
    }
    if (n.type == GFS_NODE_FREE) {
        return GFS_ENOENT;
    }
    out->type = n.type;
    out->mode = n.mode;
    out->size = n.size;
    out->nlink = n.nlink;
    out->edge_count = n.edge_count;
    out->parent = n.parent;
    return GFS_OK;
}

static int name_len_ok(const char *name, uint32_t *len_out) {
    size_t l = strlen(name);
    if (l == 0) {
        return GFS_EINVAL;
    }
    if (l > GFS_NAME_MAX) {
        return GFS_ENAMETOOLONG;
    }
    for (size_t i = 0; i < l; i++) {
        if (name[i] == '/') {
            return GFS_EINVAL;
        }
    }
    *len_out = (uint32_t)l;
    return GFS_OK;
}

int gfs_lookup(struct gfs *fs, uint64_t dir, const char *name, uint64_t *out) {
    uint32_t namelen;
    int rc = name_len_ok(name, &namelen);
    if (rc != GFS_OK) {
        return rc;
    }
    rc = map_load(fs);
    if (rc != GFS_OK) {
        return rc;
    }
    struct disk_node d;
    rc = node_read(fs, dir, &d);
    if (rc != GFS_OK) {
        return rc;
    }
    if (d.type != GFS_NODE_NAMESPACE) {
        return GFS_ENOTDIR;
    }
    return edge_lookup(fs, &d, name, namelen, out);
}

int gfs_resolve(struct gfs *fs, const char *path, uint64_t *out) {
    if (path[0] != '/') {
        return GFS_EINVAL;
    }
    int rc = map_load(fs);
    if (rc != GFS_OK) {
        return rc;
    }
    uint64_t cur = fs->root_node;
    const char *p = path;
    while (*p != '\0') {
        while (*p == '/') {
            p++;
        }
        if (*p == '\0') {
            break;
        }
        char comp[GFS_NAME_MAX + 1];
        size_t n = 0;
        while (p[n] != '\0' && p[n] != '/') {
            if (n >= GFS_NAME_MAX) {
                return GFS_ENAMETOOLONG;
            }
            comp[n] = p[n];
            n++;
        }
        comp[n] = '\0';
        p += n;

        struct disk_node d;
        rc = node_read(fs, cur, &d);
        if (rc != GFS_OK) {
            return rc;
        }
        if (d.type != GFS_NODE_NAMESPACE) {
            return GFS_ENOTDIR;
        }
        rc = edge_lookup(fs, &d, comp, (uint32_t)n, &cur);
        if (rc != GFS_OK) {
            return rc;
        }
    }
    *out = cur;
    return GFS_OK;
}

int gfs_edge_get(struct gfs *fs, uint64_t id, uint32_t index, struct gfs_edge *out) {
    int rc = map_load(fs);
    if (rc != GFS_OK) {
        return rc;
    }
    struct disk_node n;
    rc = node_read(fs, id, &n);
    if (rc != GFS_OK) {
        return rc;
    }
    if (n.type == GFS_NODE_FREE) {
        return GFS_ENOENT;
    }
    if (index >= n.edge_count) {
        return GFS_EINVAL;
    }
    struct gfs_bp cur = n.edge_head;
    uint32_t running = 0;
    while (cur.phys != 0) {
        rc = read_meta(fs, cur, fs->a);
        if (rc != GFS_OK) {
            return rc;
        }
        uint32_t count = rd32(fs->a + EB_COUNT);
        struct gfs_bp next = {rd64(fs->a + EB_NEXT_PHYS), rd32(fs->a + EB_NEXT_CRC)};
        if (index < running + count) {
            const uint8_t *rec = fs->a + EB_RECORDS + ((index - running) * ER_SIZE);
            uint32_t namelen = rd32(rec + ER_NAMELEN);
            if (namelen > GFS_NAME_MAX) {
                return GFS_ECORRUPT;
            }
            out->type = rd32(rec + ER_TYPE);
            out->target = rd64(rec + ER_TARGET);
            memcpy(out->name, rec + ER_NAME, namelen);
            out->name[namelen] = '\0';
            return GFS_OK;
        }
        running += count;
        cur = next;
    }
    return GFS_ECORRUPT; /* edge_count disagreed with the chain */
}

/* Map a file logical block to a physical block, or 0 for a hole. */
static uint64_t bmap(const struct disk_node *n, uint64_t lbn) {
    for (uint32_t k = 0; k < n->n_extents; k++) {
        const struct gfs_extent *e = &n->ext[k];
        if (lbn >= e->logical && lbn < e->logical + e->len) {
            return e->phys + (lbn - e->logical);
        }
    }
    return 0;
}

long gfs_read(struct gfs *fs, uint64_t id, uint64_t off, void *buf, size_t len) {
    int rc = map_load(fs);
    if (rc != GFS_OK) {
        return rc;
    }
    struct disk_node n;
    rc = node_read(fs, id, &n);
    if (rc != GFS_OK) {
        return rc;
    }
    if (n.type == GFS_NODE_FREE) {
        return GFS_ENOENT;
    }
    if (n.type != GFS_NODE_DATA) {
        return GFS_EISDIR;
    }
    if (off >= n.size) {
        return 0;
    }
    uint64_t avail = n.size - off;
    if (len > avail) {
        len = (size_t)avail;
    }
    uint8_t *dst = (uint8_t *)buf;
    size_t done = 0;
    while (done < len) {
        uint64_t pos = off + done;
        uint64_t lbn = pos / GFS_BLOCK_SIZE;
        uint32_t boff = (uint32_t)(pos % GFS_BLOCK_SIZE);
        size_t chunk = GFS_BLOCK_SIZE - boff;
        if (chunk > len - done) {
            chunk = len - done;
        }
        uint64_t phys = bmap(&n, lbn);
        if (phys == 0) {
            memset(dst + done, 0, chunk);
        } else {
            rc = bread(fs, phys, fs->b);
            if (rc != GFS_OK) {
                return rc;
            }
            memcpy(dst + done, fs->b + boff, chunk);
        }
        done += chunk;
    }
    return (long)len;
}

/* ---- writing ----------------------------------------------------------- */

int gfs_node_create(struct gfs *fs, uint32_t type, uint32_t mode, uint64_t *out) {
    if (type != GFS_NODE_NAMESPACE && type != GFS_NODE_DATA) {
        return GFS_EINVAL;
    }
    int rc = txn_begin(fs);
    if (rc != GFS_OK) {
        return rc;
    }
    /* Find a FREE id (skip id 0). */
    uint64_t id = 0;
    for (uint64_t bi = 0; bi * GFS_NODES_PER_BLOCK < fs->node_count && id == 0; bi++) {
        struct gfs_bp bp = map_get(fs, bi);
        if (bp.phys == 0) {
            uint64_t base = bi * GFS_NODES_PER_BLOCK;
            id = base == 0 ? 1 : base;
            break;
        }
        rc = read_meta(fs, bp, fs->tbl);
        if (rc != GFS_OK) {
            return rc;
        }
        for (uint32_t k = 0; k < GFS_NODES_PER_BLOCK; k++) {
            uint64_t cand = (bi * GFS_NODES_PER_BLOCK) + k;
            if (cand == 0 || cand >= fs->node_count) {
                continue;
            }
            if (rd32(fs->tbl + (k * GFS_NODE_SIZE) + ND_TYPE) == GFS_NODE_FREE) {
                id = cand;
                break;
            }
        }
    }
    if (id == 0 || id >= fs->node_count) {
        return GFS_ENOSPC;
    }

    struct disk_node n = {0};
    n.type = type;
    n.mode = mode;
    if (type == GFS_NODE_NAMESPACE) {
        n.parent = 0; /* set when linked into the namespace */
    }
    rc = node_write(fs, id, &n);
    if (rc != GFS_OK) {
        return rc;
    }
    rc = txn_commit(fs, -1);
    if (rc != GFS_OK) {
        return rc;
    }
    *out = id;
    return GFS_OK;
}

int gfs_link(struct gfs *fs, uint64_t dir, const char *name, uint64_t target, uint32_t type) {
    if (type != GFS_EDGE_NAME && type != GFS_EDGE_TAG && type != GFS_EDGE_REF) {
        return GFS_EINVAL;
    }
    uint32_t namelen;
    int rc = name_len_ok(name, &namelen);
    if (rc != GFS_OK) {
        return rc;
    }
    rc = txn_begin(fs);
    if (rc != GFS_OK) {
        return rc;
    }
    struct disk_node d;
    rc = node_read(fs, dir, &d);
    if (rc != GFS_OK) {
        return rc;
    }
    struct disk_node t;
    rc = node_read(fs, target, &t);
    if (rc != GFS_OK) {
        return rc;
    }
    if (t.type == GFS_NODE_FREE) {
        return GFS_ENOENT;
    }

    if (type == GFS_EDGE_NAME) {
        if (d.type != GFS_NODE_NAMESPACE) {
            return GFS_ENOTDIR;
        }
        uint64_t dummy;
        rc = edge_lookup(fs, &d, name, namelen, &dummy);
        if (rc == GFS_OK) {
            return GFS_EEXIST;
        }
        if (rc != GFS_ENOENT) {
            return rc;
        }
        if (t.type == GFS_NODE_NAMESPACE && t.nlink >= 1) {
            return GFS_EMANYPARENTS;
        }
    }

    rc = edge_add(fs, &d, type, target, name, namelen);
    if (rc != GFS_OK) {
        return rc;
    }

    if (type == GFS_EDGE_NAME) {
        t.nlink++;
        if (t.type == GFS_NODE_NAMESPACE) {
            t.parent = dir;
        }
        rc = node_write(fs, target, &t);
        if (rc != GFS_OK) {
            return rc;
        }
    }
    rc = node_write(fs, dir, &d);
    if (rc != GFS_OK) {
        return rc;
    }
    return txn_commit(fs, 0);
}

int gfs_unlink(struct gfs *fs, uint64_t dir, const char *name) {
    uint32_t namelen;
    int rc = name_len_ok(name, &namelen);
    if (rc != GFS_OK) {
        return rc;
    }
    rc = txn_begin(fs);
    if (rc != GFS_OK) {
        return rc;
    }
    struct disk_node d;
    rc = node_read(fs, dir, &d);
    if (rc != GFS_OK) {
        return rc;
    }
    if (d.type != GFS_NODE_NAMESPACE) {
        return GFS_ENOTDIR;
    }

    /* Peek at the target to enforce rmdir-empty before mutating anything. */
    uint64_t target;
    rc = edge_lookup(fs, &d, name, namelen, &target);
    if (rc != GFS_OK) {
        return rc;
    }
    struct disk_node t;
    rc = node_read(fs, target, &t);
    if (rc != GFS_OK) {
        return rc;
    }
    if (t.type == GFS_NODE_NAMESPACE && t.edge_count > 0) {
        return GFS_ENOTEMPTY;
    }

    uint64_t removed;
    rc = edge_remove_name(fs, &d, name, namelen, &removed);
    if (rc != GFS_OK) {
        return rc;
    }
    rc = node_write(fs, dir, &d);
    if (rc != GFS_OK) {
        return rc;
    }

    long nodes_delta = 0;
    t.nlink--;
    if (t.nlink == 0) {
        /* Reclaim the node: its data extents and any edge blocks. */
        for (uint32_t k = 0; k < t.n_extents; k++) {
            for (uint32_t j = 0; j < t.ext[k].len; j++) {
                free_block(fs, t.ext[k].phys + j);
            }
        }
        rc = edge_chain_free(fs, t.edge_head);
        if (rc != GFS_OK) {
            return rc;
        }
        struct disk_node freed = {0};
        freed.type = GFS_NODE_FREE;
        rc = node_write(fs, target, &freed);
        if (rc != GFS_OK) {
            return rc;
        }
        nodes_delta = 1;
    } else {
        rc = node_write(fs, target, &t);
        if (rc != GFS_OK) {
            return rc;
        }
    }
    return txn_commit(fs, nodes_delta);
}

long gfs_write(struct gfs *fs, uint64_t id, uint64_t off, const void *buf, size_t len) {
    if (len == 0) {
        return 0;
    }
    if (off > GFS_MAX_FILE_SIZE || len > GFS_MAX_FILE_SIZE || off + len > GFS_MAX_FILE_SIZE) {
        return GFS_EFBIG;
    }
    int rc = txn_begin(fs);
    if (rc != GFS_OK) {
        return rc;
    }
    struct disk_node n;
    rc = node_read(fs, id, &n);
    if (rc != GFS_OK) {
        return rc;
    }
    if (n.type == GFS_NODE_FREE) {
        return GFS_ENOENT;
    }
    if (n.type != GFS_NODE_DATA) {
        return GFS_EISDIR;
    }

    uint64_t new_size = n.size > off + len ? n.size : off + len;
    uint64_t new_blocks = (new_size + GFS_BLOCK_SIZE - 1) / GFS_BLOCK_SIZE;

    /* Physical map of every logical block, from existing extents. */
    uint64_t *pmap = (uint64_t *)fs->a; /* up to 512 entries */
    memset(fs->a, 0, GFS_BLOCK_SIZE);
    for (uint32_t k = 0; k < n.n_extents; k++) {
        for (uint32_t j = 0; j < n.ext[k].len; j++) {
            uint64_t lbn = n.ext[k].logical + j;
            if (lbn < GFS_MAX_FILE_BLOCKS) {
                pmap[lbn] = n.ext[k].phys + j;
            }
        }
    }

    uint64_t lbn_first = off / GFS_BLOCK_SIZE;
    uint64_t lbn_last = (off + len - 1) / GFS_BLOCK_SIZE;
    uint64_t needed = lbn_last - lbn_first + 1;
    if (count_free(fs) < needed + 2) { /* + node table + node map */
        return GFS_ENOSPC;
    }

    const uint8_t *src = (const uint8_t *)buf;
    uint64_t hint = 0;
    for (uint64_t lbn = lbn_first; lbn <= lbn_last; lbn++) {
        uint64_t oldp = pmap[lbn];
        uint64_t bstart = lbn * GFS_BLOCK_SIZE;
        uint64_t wstart = off > bstart ? off : bstart;
        uint64_t wend = off + len < bstart + GFS_BLOCK_SIZE ? off + len : bstart + GFS_BLOCK_SIZE;
        uint32_t boff = (uint32_t)(wstart - bstart);
        uint32_t wlen = (uint32_t)(wend - wstart);

        if (wlen < GFS_BLOCK_SIZE) {
            if (oldp != 0) {
                rc = bread(fs, oldp, fs->b);
                if (rc != GFS_OK) {
                    return rc;
                }
            } else {
                memset(fs->b, 0, GFS_BLOCK_SIZE);
            }
        }
        memcpy(fs->b + boff, src + (wstart - off), wlen);

        uint64_t np;
        rc = alloc_near(fs, hint, &np);
        if (rc != GFS_OK) {
            return rc;
        }
        rc = bwrite(fs, np, fs->b);
        if (rc != GFS_OK) {
            return rc;
        }
        free_block(fs, oldp);
        pmap[lbn] = np;
        hint = np + 1;
    }

    /* Coalesce the physical map back into inline extents. */
    n.n_extents = 0;
    for (uint64_t lbn = 0; lbn < new_blocks; lbn++) {
        uint64_t phys = pmap[lbn];
        if (phys == 0) {
            continue;
        }
        if (n.n_extents > 0) {
            struct gfs_extent *last = &n.ext[n.n_extents - 1];
            if (last->logical + last->len == lbn && last->phys + last->len == phys) {
                last->len++;
                continue;
            }
        }
        if (n.n_extents == GFS_INLINE_EXTENTS) {
            return GFS_EFRAG;
        }
        n.ext[n.n_extents].logical = lbn;
        n.ext[n.n_extents].phys = phys;
        n.ext[n.n_extents].len = 1;
        n.n_extents++;
    }
    n.size = new_size;

    rc = node_write(fs, id, &n);
    if (rc != GFS_OK) {
        return rc;
    }
    rc = txn_commit(fs, 0);
    if (rc != GFS_OK) {
        return rc;
    }
    return (long)len;
}

/* ---- fsck -------------------------------------------------------------- */

struct fsck_ctx {
    struct gfs *fs;
    uint8_t *shadow;      /* referenced-block bitmap */
    uint32_t *nlink;      /* observed incoming NAME edges per node */
    uint32_t *obs_parent; /* observed parent (a NAME edge source) per node */
    uint8_t *present;     /* node is allocated (non-FREE) */
    size_t shadow_bytes;
    int errors;
    void (*report)(void *cookie, const char *msg);
    void *cookie;
};

static void fsck_err(struct fsck_ctx *c, const char *fmt, uint64_t a, uint64_t b) {
    char msg[128];
    snprintf(msg, sizeof(msg), fmt, (unsigned long long)a, (unsigned long long)b);
    c->errors++;
    if (c->report) {
        c->report(c->cookie, msg);
    }
}

/* Mark a metadata/data block referenced; report double-use / range errors. */
static void fsck_mark(struct fsck_ctx *c, uint64_t blk) {
    if (blk >= c->fs->total_blocks) {
        fsck_err(c, "block %llu out of range", blk, 0);
        return;
    }
    if (bit_test(c->shadow, blk)) {
        fsck_err(c, "block %llu referenced twice", blk, 0);
        return;
    }
    bit_set(c->shadow, blk);
}

size_t gfs_fsck_work_size(uint64_t node_count, uint64_t total_blocks) {
    size_t shadow = (size_t)((total_blocks + 7) / 8);
    return shadow + ((size_t)node_count * (4 + 4 + 1)) + 16;
}

int gfs_fsck(struct gfs *fs, void *work, size_t work_len,
             void (*report)(void *cookie, const char *msg), void *cookie) {
    if (work_len < gfs_fsck_work_size(fs->node_count, fs->total_blocks)) {
        return GFS_EWORKSIZE;
    }
    struct fsck_ctx c;
    memset(&c, 0, sizeof(c));
    c.fs = fs;
    c.report = report;
    c.cookie = cookie;
    c.shadow_bytes = (size_t)((fs->total_blocks + 7) / 8);

    uint8_t *p = (uint8_t *)work;
    c.shadow = p;
    p += c.shadow_bytes;
    c.nlink = (uint32_t *)p;
    p += (size_t)fs->node_count * 4;
    c.obs_parent = (uint32_t *)p;
    p += (size_t)fs->node_count * 4;
    c.present = p;
    memset(c.shadow, 0, c.shadow_bytes);
    memset(c.nlink, 0, (size_t)fs->node_count * 4);
    memset(c.obs_parent, 0, (size_t)fs->node_count * 4);
    memset(c.present, 0, (size_t)fs->node_count);

    /* Reserved blocks: two superblocks, two bitmap copies, the node map. */
    fsck_mark(&c, 0);
    fsck_mark(&c, 1);
    for (uint64_t copy = 0; copy < 2; copy++) {
        for (uint64_t j = 0; j < fs->bitmap_blocks; j++) {
            fsck_mark(&c, fs->bitmap_start + (copy * fs->bitmap_blocks) + j);
        }
    }
    fsck_mark(&c, fs->nodemap.phys);

    int rc = read_meta(fs, fs->nodemap, fs->map);
    if (rc != GFS_OK) {
        fsck_err(&c, "node map checksum/read failed", 0, 0);
        return GFS_ECORRUPT;
    }

    /* Pass 1: every table block and node record. */
    uint8_t tbl[GFS_BLOCK_SIZE];
    uint8_t eb[GFS_BLOCK_SIZE];
    for (uint64_t bi = 0; bi * GFS_NODES_PER_BLOCK < fs->node_count; bi++) {
        struct gfs_bp bp = map_get(fs, bi);
        if (bp.phys == 0) {
            continue;
        }
        fsck_mark(&c, bp.phys);
        if (read_meta(fs, bp, tbl) != GFS_OK) {
            fsck_err(&c, "table block %llu checksum failed", bp.phys, 0);
            continue;
        }
        for (uint32_t k = 0; k < GFS_NODES_PER_BLOCK; k++) {
            uint64_t id = (bi * GFS_NODES_PER_BLOCK) + k;
            if (id == 0 || id >= fs->node_count) {
                continue;
            }
            struct disk_node n;
            node_decode(tbl + (k * GFS_NODE_SIZE), &n);
            if (n.type == GFS_NODE_FREE) {
                continue;
            }
            if (n.type != GFS_NODE_NAMESPACE && n.type != GFS_NODE_DATA) {
                fsck_err(&c, "node %llu has bad type %llu", id, n.type);
                continue;
            }
            c.present[id] = 1;
            if (n.n_extents > GFS_INLINE_EXTENTS) {
                fsck_err(&c, "node %llu has %llu extents", id, n.n_extents);
                n.n_extents = GFS_INLINE_EXTENTS;
            }
            if (n.type == GFS_NODE_NAMESPACE && n.n_extents != 0) {
                fsck_err(&c, "namespace %llu has data extents", id, 0);
            }
            for (uint32_t e = 0; e < n.n_extents; e++) {
                for (uint32_t j = 0; j < n.ext[e].len; j++) {
                    fsck_mark(&c, n.ext[e].phys + j);
                }
            }
            /* Walk the edge chain. */
            struct gfs_bp cur = n.edge_head;
            uint32_t seen = 0;
            uint32_t guard = 0;
            while (cur.phys != 0 && guard++ < fs->total_blocks) {
                fsck_mark(&c, cur.phys);
                if (read_meta(fs, cur, eb) != GFS_OK) {
                    fsck_err(&c, "edge block %llu checksum failed", cur.phys, 0);
                    break;
                }
                uint32_t count = rd32(eb + EB_COUNT);
                if (count > GFS_EDGES_PER_BLOCK) {
                    fsck_err(&c, "edge block %llu count %llu", cur.phys, count);
                    count = GFS_EDGES_PER_BLOCK;
                }
                for (uint32_t r = 0; r < count; r++) {
                    const uint8_t *rec = eb + EB_RECORDS + (r * ER_SIZE);
                    uint64_t tgt = rd64(rec + ER_TARGET);
                    if (rd32(rec + ER_TYPE) == GFS_EDGE_NAME && tgt > 0 && tgt < fs->node_count) {
                        c.nlink[tgt]++;
                        c.obs_parent[tgt] = (uint32_t)id;
                    }
                    seen++;
                }
                cur.phys = rd64(eb + EB_NEXT_PHYS);
                cur.crc = rd32(eb + EB_NEXT_CRC);
            }
            if (seen != n.edge_count) {
                fsck_err(&c, "node %llu edge_count %llu", id, n.edge_count);
            }
        }
    }

    /* Pass 2: cross-checks that need the full node census. */
    for (uint64_t id = 1; id < fs->node_count; id++) {
        if (c.nlink[id] > 0 && !c.present[id]) {
            fsck_err(&c, "NAME edge to free node %llu", id, 0);
        }
    }
    /* Re-scan present nodes for nlink and single-parent invariants. */
    for (uint64_t bi = 0; bi * GFS_NODES_PER_BLOCK < fs->node_count; bi++) {
        struct gfs_bp bp = map_get(fs, bi);
        if (bp.phys == 0 || read_meta(fs, bp, tbl) != GFS_OK) {
            continue;
        }
        for (uint32_t k = 0; k < GFS_NODES_PER_BLOCK; k++) {
            uint64_t id = (bi * GFS_NODES_PER_BLOCK) + k;
            if (id == 0 || id >= fs->node_count || !c.present[id]) {
                continue;
            }
            struct disk_node n;
            node_decode(tbl + (k * GFS_NODE_SIZE), &n);
            if (n.nlink != c.nlink[id]) {
                fsck_err(&c, "node %llu nlink %llu", id, n.nlink);
            }
            if (n.type == GFS_NODE_NAMESPACE) {
                if (id == fs->root_node) {
                    if (n.parent != fs->root_node) {
                        fsck_err(&c, "root parent %llu", n.parent, 0);
                    }
                } else {
                    if (c.nlink[id] != 1) {
                        fsck_err(&c, "namespace %llu has %llu parents", id, c.nlink[id]);
                    }
                    if (n.parent != c.obs_parent[id]) {
                        fsck_err(&c, "namespace %llu parent %llu", id, n.parent);
                    }
                }
            }
        }
    }

    /* Compare the reachable set against the on-disk allocation bitmap. */
    uint8_t live_bm[GFS_BLOCK_SIZE];
    uint64_t parity = fs->generation & 1;
    for (uint64_t j = 0; j < fs->bitmap_blocks; j++) {
        if (bread(fs, fs->bitmap_start + (parity * fs->bitmap_blocks) + j, live_bm) != GFS_OK) {
            fsck_err(&c, "bitmap read failed", 0, 0);
            break;
        }
        for (uint64_t bit = 0; bit < BM_BITS_PER_BLOCK; bit++) {
            uint64_t blk = (j * BM_BITS_PER_BLOCK) + bit;
            if (blk >= fs->total_blocks) {
                break;
            }
            int used = (live_bm[bit >> 3] >> (bit & 7)) & 1;
            int ref = bit_test(c.shadow, blk);
            if (used && !ref) {
                fsck_err(&c, "block %llu marked used but unreferenced (leak)", blk, 0);
            } else if (!used && ref) {
                fsck_err(&c, "block %llu referenced but marked free", blk, 0);
            }
        }
    }

    return c.errors ? GFS_ECORRUPT : GFS_OK;
}

const char *gfs_strerror(int err) {
    switch (err) {
    case GFS_OK:
        return "ok";
    case GFS_EIO:
        return "block I/O error";
    case GFS_EBADFS:
        return "not a graphfs filesystem";
    case GFS_ECORRUPT:
        return "corrupt filesystem structure";
    case GFS_EBADCRC:
        return "checksum mismatch";
    case GFS_ENOENT:
        return "no such node or name";
    case GFS_EEXIST:
        return "name already exists";
    case GFS_ENOTDIR:
        return "not a namespace node";
    case GFS_EISDIR:
        return "not a data node";
    case GFS_ENOSPC:
        return "no space left";
    case GFS_EINVAL:
        return "invalid argument";
    case GFS_ENAMETOOLONG:
        return "name too long";
    case GFS_ENOTEMPTY:
        return "namespace not empty";
    case GFS_EMANYPARENTS:
        return "namespace already has a parent";
    case GFS_EFBIG:
        return "file too big";
    case GFS_EROFS:
        return "read-only filesystem";
    case GFS_ENOMEM:
        return "work buffer too small";
    case GFS_EFRAG:
        return "file too fragmented for inline extents";
    case GFS_EWORKSIZE:
        return "fsck arena too small";
    default:
        return "unknown graphfs error";
    }
}
