/*
 * test_graphfs.c - the graphfs copy-on-write core under ASan/UBSan.
 *
 * A RAM-backed block device stands in for virtio-blk; the tests format,
 * mount, mutate, and fsck real images, then reach into the raw bytes to
 * prove the integrity and crash-fallback properties the format promises:
 * a corrupted metadata block is detected by checksum, and a wrecked live
 * superblock falls back to the previous generation.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <crc32c.h>
#include <graphfs_core.h>

#include "test.h"

#define NBLK 512u /* 2 MiB scratch disk */

struct ramdisk {
    uint8_t *data;
    uint64_t nblk;
};

static int rd_read(void *ctx, uint64_t lba, void *buf) {
    struct ramdisk *d = ctx;
    if (lba >= d->nblk) {
        return -1;
    }
    memcpy(buf, d->data + (lba * GFS_BLOCK_SIZE), GFS_BLOCK_SIZE);
    return 0;
}

static int rd_write(void *ctx, uint64_t lba, const void *buf) {
    struct ramdisk *d = ctx;
    if (lba >= d->nblk) {
        return -1;
    }
    memcpy(d->data + (lba * GFS_BLOCK_SIZE), buf, GFS_BLOCK_SIZE);
    return 0;
}

static const struct gfs_ops RAM_OPS = {rd_read, rd_write};

/* Format a fresh disk and return a writable mount (out params own memory
 * the caller frees with fs_teardown). */
struct mounted {
    struct ramdisk disk;
    struct gfs fs;
    void *work;
};

static void fs_setup(struct mounted *m, uint64_t node_count) {
    m->disk.data = calloc(NBLK, GFS_BLOCK_SIZE);
    m->disk.nblk = NBLK;
    ASSERT_EQ_INT(GFS_OK, gfs_mkfs(&RAM_OPS, &m->disk, NBLK, node_count));
    size_t wl = gfs_mount_work_size(NBLK);
    m->work = malloc(wl);
    ASSERT_EQ_INT(GFS_OK, gfs_mount(&m->fs, &RAM_OPS, &m->disk, 1, m->work, wl));
}

static void fs_teardown(struct mounted *m) {
    free(m->work);
    free(m->disk.data);
}

static int fsck_clean(struct gfs *fs) {
    size_t wl = gfs_fsck_work_size(fs->node_count, fs->total_blocks);
    void *w = malloc(wl);
    int rc = gfs_fsck(fs, w, wl, NULL, NULL);
    free(w);
    return rc;
}

TEST(crc32c_known_answer) {
    /* The Castagnoli check vector. */
    ASSERT_EQ_INT((long long)0xE3069283ull, (long long)crc32c("123456789", 9));
    ASSERT_EQ_INT(0, (long long)crc32c("", 0));
}

TEST(graphfs_mkfs_and_root) {
    struct mounted m;
    fs_setup(&m, 64);

    struct gfs_node root;
    ASSERT_EQ_INT(GFS_OK, gfs_node_get(&m.fs, GFS_ROOT_NODE, &root));
    ASSERT_EQ_INT(GFS_NODE_NAMESPACE, root.type);
    ASSERT_EQ_INT(0, root.nlink);
    ASSERT_EQ_INT(0, root.edge_count);

    uint64_t id;
    ASSERT_EQ_INT(GFS_OK, gfs_resolve(&m.fs, "/", &id));
    ASSERT_EQ_INT(GFS_ROOT_NODE, (long long)id);
    ASSERT_EQ_INT(GFS_OK, fsck_clean(&m.fs));
    fs_teardown(&m);
}

TEST(graphfs_file_write_read) {
    struct mounted m;
    fs_setup(&m, 64);

    uint64_t file;
    ASSERT_EQ_INT(GFS_OK, gfs_node_create(&m.fs, GFS_NODE_DATA, 0644, &file));
    ASSERT_EQ_INT(GFS_OK, gfs_link(&m.fs, GFS_ROOT_NODE, "hello", file, GFS_EDGE_NAME));

    const char *msg = "graphfs is copy-on-write";
    ASSERT_EQ_INT((long long)strlen(msg), gfs_write(&m.fs, file, 0, msg, strlen(msg)));

    uint64_t r;
    ASSERT_EQ_INT(GFS_OK, gfs_resolve(&m.fs, "/hello", &r));
    ASSERT_EQ_INT((long long)file, (long long)r);

    char buf[64] = {0};
    ASSERT_EQ_INT((long long)strlen(msg), gfs_read(&m.fs, file, 0, buf, sizeof(buf)));
    ASSERT_EQ_INT(0, memcmp(buf, msg, strlen(msg)));

    struct gfs_node n;
    ASSERT_EQ_INT(GFS_OK, gfs_node_get(&m.fs, file, &n));
    ASSERT_EQ_INT((long long)strlen(msg), (long long)n.size);
    ASSERT_EQ_INT(1, n.nlink);

    /* Offset read, short at EOF. */
    ASSERT_EQ_INT(3, gfs_read(&m.fs, file, (uint64_t)strlen(msg) - 3, buf, 16));
    ASSERT_EQ_INT(GFS_OK, fsck_clean(&m.fs));
    fs_teardown(&m);
}

TEST(graphfs_multiblock_write) {
    struct mounted m;
    fs_setup(&m, 64);

    uint64_t file;
    ASSERT_EQ_INT(GFS_OK, gfs_node_create(&m.fs, GFS_NODE_DATA, 0644, &file));
    ASSERT_EQ_INT(GFS_OK, gfs_link(&m.fs, GFS_ROOT_NODE, "big", file, GFS_EDGE_NAME));

    /* ~10 blocks, written sequentially: the allocator should keep it to a
     * single extent, and the read must reproduce it exactly. */
    size_t len = (10 * GFS_BLOCK_SIZE) + 123;
    uint8_t *src = malloc(len);
    for (size_t i = 0; i < len; i++) {
        src[i] = (uint8_t)((i * 31) + 7);
    }
    ASSERT_EQ_INT((long long)len, gfs_write(&m.fs, file, 0, src, len));

    uint8_t *got = calloc(1, len);
    ASSERT_EQ_INT((long long)len, gfs_read(&m.fs, file, 0, got, len));
    ASSERT_EQ_INT(0, memcmp(src, got, len));

    /* Overwrite a middle block; the rest stays intact. */
    uint8_t patch[GFS_BLOCK_SIZE];
    memset(patch, 0xAB, sizeof(patch));
    ASSERT_EQ_INT((long long)sizeof(patch),
                  gfs_write(&m.fs, file, 4 * GFS_BLOCK_SIZE, patch, sizeof(patch)));
    memcpy(src + (4 * GFS_BLOCK_SIZE), patch, sizeof(patch));
    ASSERT_EQ_INT((long long)len, gfs_read(&m.fs, file, 0, got, len));
    ASSERT_EQ_INT(0, memcmp(src, got, len));

    ASSERT_EQ_INT(GFS_OK, fsck_clean(&m.fs));
    free(src);
    free(got);
    fs_teardown(&m);
}

TEST(graphfs_directories) {
    struct mounted m;
    fs_setup(&m, 64);

    uint64_t bin;
    ASSERT_EQ_INT(GFS_OK, gfs_node_create(&m.fs, GFS_NODE_NAMESPACE, 0755, &bin));
    ASSERT_EQ_INT(GFS_OK, gfs_link(&m.fs, GFS_ROOT_NODE, "bin", bin, GFS_EDGE_NAME));

    uint64_t prog;
    ASSERT_EQ_INT(GFS_OK, gfs_node_create(&m.fs, GFS_NODE_DATA, 0755, &prog));
    ASSERT_EQ_INT(GFS_OK, gfs_link(&m.fs, bin, "init", prog, GFS_EDGE_NAME));

    uint64_t r;
    ASSERT_EQ_INT(GFS_OK, gfs_resolve(&m.fs, "/bin/init", &r));
    ASSERT_EQ_INT((long long)prog, (long long)r);
    ASSERT_EQ_INT(GFS_OK, gfs_resolve(&m.fs, "///bin//init", &r)); /* extra slashes */
    ASSERT_EQ_INT((long long)prog, (long long)r);

    /* The namespace records its single parent. */
    struct gfs_node n;
    ASSERT_EQ_INT(GFS_OK, gfs_node_get(&m.fs, bin, &n));
    ASSERT_EQ_INT((long long)GFS_ROOT_NODE, (long long)n.parent);
    ASSERT_EQ_INT(1, n.nlink);

    ASSERT_EQ_INT(GFS_OK, fsck_clean(&m.fs));
    fs_teardown(&m);
}

TEST(graphfs_error_paths) {
    struct mounted m;
    fs_setup(&m, 64);

    uint64_t a;
    ASSERT_EQ_INT(GFS_OK, gfs_node_create(&m.fs, GFS_NODE_DATA, 0, &a));
    ASSERT_EQ_INT(GFS_OK, gfs_link(&m.fs, GFS_ROOT_NODE, "a", a, GFS_EDGE_NAME));

    /* Duplicate name. */
    ASSERT_EQ_INT(GFS_EEXIST, gfs_link(&m.fs, GFS_ROOT_NODE, "a", a, GFS_EDGE_NAME));
    /* Missing name / path. */
    uint64_t r;
    ASSERT_EQ_INT(GFS_ENOENT, gfs_resolve(&m.fs, "/nope", &r));
    /* Descend through a file. */
    ASSERT_EQ_INT(GFS_ENOTDIR, gfs_resolve(&m.fs, "/a/b", &r));
    /* Bad names. */
    ASSERT_EQ_INT(GFS_EINVAL, gfs_lookup(&m.fs, GFS_ROOT_NODE, "", &r));

    /* Single-parent namespaces: a second NAME edge is refused. */
    uint64_t d1, d2;
    ASSERT_EQ_INT(GFS_OK, gfs_node_create(&m.fs, GFS_NODE_NAMESPACE, 0, &d1));
    ASSERT_EQ_INT(GFS_OK, gfs_link(&m.fs, GFS_ROOT_NODE, "d1", d1, GFS_EDGE_NAME));
    ASSERT_EQ_INT(GFS_OK, gfs_node_create(&m.fs, GFS_NODE_NAMESPACE, 0, &d2));
    ASSERT_EQ_INT(GFS_OK, gfs_link(&m.fs, GFS_ROOT_NODE, "d2", d2, GFS_EDGE_NAME));
    ASSERT_EQ_INT(GFS_EMANYPARENTS, gfs_link(&m.fs, d2, "again", d1, GFS_EDGE_NAME));

    /* rmdir on a non-empty namespace. */
    uint64_t child;
    ASSERT_EQ_INT(GFS_OK, gfs_node_create(&m.fs, GFS_NODE_DATA, 0, &child));
    ASSERT_EQ_INT(GFS_OK, gfs_link(&m.fs, d1, "c", child, GFS_EDGE_NAME));
    ASSERT_EQ_INT(GFS_ENOTEMPTY, gfs_unlink(&m.fs, GFS_ROOT_NODE, "d1"));

    ASSERT_EQ_INT(GFS_OK, fsck_clean(&m.fs));
    fs_teardown(&m);
}

TEST(graphfs_hardlink_and_free) {
    struct mounted m;
    fs_setup(&m, 64);

    struct gfs_node root0;
    ASSERT_EQ_INT(GFS_OK, gfs_node_get(&m.fs, GFS_ROOT_NODE, &root0));
    uint64_t free_nodes0 = m.fs.free_nodes;

    uint64_t file;
    ASSERT_EQ_INT(GFS_OK, gfs_node_create(&m.fs, GFS_NODE_DATA, 0, &file));
    ASSERT_EQ_INT(GFS_OK, gfs_link(&m.fs, GFS_ROOT_NODE, "one", file, GFS_EDGE_NAME));
    ASSERT_EQ_INT(GFS_OK, gfs_link(&m.fs, GFS_ROOT_NODE, "two", file, GFS_EDGE_NAME));
    const char *data = "shared";
    ASSERT_EQ_INT((long long)strlen(data), gfs_write(&m.fs, file, 0, data, strlen(data)));

    struct gfs_node n;
    ASSERT_EQ_INT(GFS_OK, gfs_node_get(&m.fs, file, &n));
    ASSERT_EQ_INT(2, n.nlink);

    /* Drop one link: data survives via the other name. */
    ASSERT_EQ_INT(GFS_OK, gfs_unlink(&m.fs, GFS_ROOT_NODE, "one"));
    ASSERT_EQ_INT(GFS_OK, gfs_node_get(&m.fs, file, &n));
    ASSERT_EQ_INT(1, n.nlink);
    char buf[16] = {0};
    ASSERT_EQ_INT((long long)strlen(data), gfs_read(&m.fs, file, 0, buf, sizeof(buf)));
    ASSERT_EQ_INT(0, memcmp(buf, data, strlen(data)));

    /* Drop the last link: the node is reclaimed. */
    ASSERT_EQ_INT(GFS_OK, gfs_unlink(&m.fs, GFS_ROOT_NODE, "two"));
    ASSERT_EQ_INT(GFS_ENOENT, gfs_node_get(&m.fs, file, &n));
    ASSERT_EQ_INT((long long)free_nodes0, (long long)m.fs.free_nodes);

    ASSERT_EQ_INT(GFS_OK, fsck_clean(&m.fs));
    fs_teardown(&m);
}

TEST(graphfs_checksum_detects_corruption) {
    struct mounted m;
    fs_setup(&m, 64);
    uint64_t file;
    ASSERT_EQ_INT(GFS_OK, gfs_node_create(&m.fs, GFS_NODE_DATA, 0, &file));
    ASSERT_EQ_INT(GFS_OK, gfs_link(&m.fs, GFS_ROOT_NODE, "x", file, GFS_EDGE_NAME));

    /* Flip a byte in the live node-map block: the next read must catch it
     * by checksum rather than return garbage. */
    uint64_t nm = m.fs.nodemap.phys;
    m.disk.data[(nm * GFS_BLOCK_SIZE) + 17] ^= 0xFF;

    struct gfs_node n;
    ASSERT_EQ_INT(GFS_EBADCRC, gfs_node_get(&m.fs, file, &n));
    fs_teardown(&m);
}

TEST(graphfs_dual_superblock_fallback) {
    struct mounted m;
    fs_setup(&m, 64);

    /* A few commits so the live generation is well past mkfs. */
    for (int i = 0; i < 4; i++) {
        uint64_t id;
        char name[8];
        snprintf(name, sizeof(name), "f%d", i);
        ASSERT_EQ_INT(GFS_OK, gfs_node_create(&m.fs, GFS_NODE_DATA, 0, &id));
        ASSERT_EQ_INT(GFS_OK, gfs_link(&m.fs, GFS_ROOT_NODE, name, id, GFS_EDGE_NAME));
    }
    uint64_t live_gen = m.fs.generation;
    uint64_t live_slot = live_gen & 1;

    /* Wreck the live superblock slot; the previous generation, intact in
     * the other slot, must take over on remount. */
    m.disk.data[(live_slot * GFS_BLOCK_SIZE) + 40] ^= 0xFF; /* mangles the crc region */

    struct gfs fs2;
    size_t wl = gfs_mount_work_size(NBLK);
    void *work2 = malloc(wl);
    ASSERT_EQ_INT(GFS_OK, gfs_mount(&fs2, &RAM_OPS, &m.disk, 1, work2, wl));
    ASSERT_EQ_INT((long long)(live_gen - 1), (long long)fs2.generation);

    struct gfs_node root;
    ASSERT_EQ_INT(GFS_OK, gfs_node_get(&fs2, GFS_ROOT_NODE, &root));
    ASSERT_EQ_INT(GFS_NODE_NAMESPACE, root.type);
    ASSERT_EQ_INT(GFS_OK, fsck_clean(&fs2));

    free(work2);
    fs_teardown(&m);
}

TEST(graphfs_read_only_mount) {
    struct mounted m;
    fs_setup(&m, 64);
    uint64_t file;
    ASSERT_EQ_INT(GFS_OK, gfs_node_create(&m.fs, GFS_NODE_DATA, 0, &file));
    ASSERT_EQ_INT(GFS_OK, gfs_link(&m.fs, GFS_ROOT_NODE, "ro", file, GFS_EDGE_NAME));

    struct gfs fs2;
    ASSERT_EQ_INT(GFS_OK, gfs_mount(&fs2, &RAM_OPS, &m.disk, 0, NULL, 0));
    uint64_t r;
    ASSERT_EQ_INT(GFS_OK, gfs_resolve(&fs2, "/ro", &r));
    ASSERT_EQ_INT((long long)file, (long long)r);
    /* Writes are refused on a read-only mount. */
    ASSERT_EQ_INT(GFS_EROFS, gfs_node_create(&fs2, GFS_NODE_DATA, 0, &r));
    fs_teardown(&m);
}

TEST(graphfs_create_at) {
    struct mounted m;
    fs_setup(&m, 64);
    uint64_t gen0 = m.fs.generation;

    /* One call, one transaction: node exists, is linked, has its parent. */
    uint64_t dir;
    ASSERT_EQ_INT(GFS_OK, gfs_create_at(&m.fs, GFS_ROOT_NODE, "d", GFS_NODE_NAMESPACE, 0755, &dir));
    ASSERT_EQ_INT((long long)(gen0 + 1), (long long)m.fs.generation);
    struct gfs_node n;
    ASSERT_EQ_INT(GFS_OK, gfs_node_get(&m.fs, dir, &n));
    ASSERT_EQ_INT(GFS_NODE_NAMESPACE, n.type);
    ASSERT_EQ_INT(1, n.nlink);
    ASSERT_EQ_INT((long long)GFS_ROOT_NODE, (long long)n.parent);

    uint64_t file;
    ASSERT_EQ_INT(GFS_OK, gfs_create_at(&m.fs, dir, "f", GFS_NODE_DATA, 0644, &file));
    uint64_t r;
    ASSERT_EQ_INT(GFS_OK, gfs_resolve(&m.fs, "/d/f", &r));
    ASSERT_EQ_INT((long long)file, (long long)r);

    /* Error paths mirror gfs_link. */
    ASSERT_EQ_INT(GFS_EEXIST, gfs_create_at(&m.fs, dir, "f", GFS_NODE_DATA, 0, &r));
    ASSERT_EQ_INT(GFS_ENOTDIR, gfs_create_at(&m.fs, file, "x", GFS_NODE_DATA, 0, &r));
    ASSERT_EQ_INT(GFS_EINVAL, gfs_create_at(&m.fs, dir, "bad/name", GFS_NODE_DATA, 0, &r));

    ASSERT_EQ_INT(GFS_OK, fsck_clean(&m.fs));
    fs_teardown(&m);
}

TEST(graphfs_rename_basics) {
    struct mounted m;
    fs_setup(&m, 64);

    uint64_t a, b, f;
    ASSERT_EQ_INT(GFS_OK, gfs_create_at(&m.fs, GFS_ROOT_NODE, "a", GFS_NODE_NAMESPACE, 0755, &a));
    ASSERT_EQ_INT(GFS_OK, gfs_create_at(&m.fs, GFS_ROOT_NODE, "b", GFS_NODE_NAMESPACE, 0755, &b));
    ASSERT_EQ_INT(GFS_OK, gfs_create_at(&m.fs, a, "f", GFS_NODE_DATA, 0644, &f));
    const char *data = "renamed but intact";
    ASSERT_EQ_INT((long long)strlen(data), gfs_write(&m.fs, f, 0, data, strlen(data)));

    /* Same-directory rename. */
    ASSERT_EQ_INT(GFS_OK, gfs_rename(&m.fs, a, "f", a, "g"));
    uint64_t r;
    ASSERT_EQ_INT(GFS_ENOENT, gfs_resolve(&m.fs, "/a/f", &r));
    ASSERT_EQ_INT(GFS_OK, gfs_resolve(&m.fs, "/a/g", &r));
    ASSERT_EQ_INT((long long)f, (long long)r);

    /* Cross-directory move keeps the node id and the bytes. */
    ASSERT_EQ_INT(GFS_OK, gfs_rename(&m.fs, a, "g", b, "h"));
    ASSERT_EQ_INT(GFS_OK, gfs_resolve(&m.fs, "/b/h", &r));
    ASSERT_EQ_INT((long long)f, (long long)r);
    char buf[32] = {0};
    ASSERT_EQ_INT((long long)strlen(data), gfs_read(&m.fs, f, 0, buf, sizeof(buf)));
    ASSERT_EQ_INT(0, memcmp(buf, data, strlen(data)));

    /* Moving a directory rewrites its parent pointer. */
    ASSERT_EQ_INT(GFS_OK, gfs_rename(&m.fs, GFS_ROOT_NODE, "a", b, "a"));
    struct gfs_node n;
    ASSERT_EQ_INT(GFS_OK, gfs_node_get(&m.fs, a, &n));
    ASSERT_EQ_INT((long long)b, (long long)n.parent);
    ASSERT_EQ_INT(GFS_OK, gfs_resolve(&m.fs, "/b/a", &r));
    ASSERT_EQ_INT((long long)a, (long long)r);

    /* Missing source. */
    ASSERT_EQ_INT(GFS_ENOENT, gfs_rename(&m.fs, b, "nope", b, "x"));

    ASSERT_EQ_INT(GFS_OK, fsck_clean(&m.fs));
    fs_teardown(&m);
}

TEST(graphfs_rename_replace) {
    struct mounted m;
    fs_setup(&m, 64);
    uint64_t free_nodes0 = m.fs.free_nodes;
    uint64_t free_blocks0 = m.fs.free_blocks;

    /* Replacing a file frees it — node and data blocks both. */
    uint64_t f1, f2;
    ASSERT_EQ_INT(GFS_OK, gfs_create_at(&m.fs, GFS_ROOT_NODE, "f1", GFS_NODE_DATA, 0644, &f1));
    ASSERT_EQ_INT(GFS_OK, gfs_create_at(&m.fs, GFS_ROOT_NODE, "f2", GFS_NODE_DATA, 0644, &f2));
    uint8_t blob[2 * GFS_BLOCK_SIZE];
    memset(blob, 0x7E, sizeof(blob));
    ASSERT_EQ_INT((long long)sizeof(blob), gfs_write(&m.fs, f2, 0, blob, sizeof(blob)));
    ASSERT_EQ_INT(GFS_OK, gfs_rename(&m.fs, GFS_ROOT_NODE, "f1", GFS_ROOT_NODE, "f2"));
    struct gfs_node n;
    ASSERT_EQ_INT(GFS_ENOENT, gfs_node_get(&m.fs, f2, &n)); /* freed */
    uint64_t r;
    ASSERT_EQ_INT(GFS_OK, gfs_resolve(&m.fs, "/f2", &r));
    ASSERT_EQ_INT((long long)f1, (long long)r);

    /* A directory replaces only an *empty* directory. */
    uint64_t d1, d2, d3;
    ASSERT_EQ_INT(GFS_OK, gfs_create_at(&m.fs, GFS_ROOT_NODE, "d1", GFS_NODE_NAMESPACE, 0, &d1));
    ASSERT_EQ_INT(GFS_OK, gfs_create_at(&m.fs, GFS_ROOT_NODE, "d2", GFS_NODE_NAMESPACE, 0, &d2));
    ASSERT_EQ_INT(GFS_OK, gfs_create_at(&m.fs, GFS_ROOT_NODE, "d3", GFS_NODE_NAMESPACE, 0, &d3));
    uint64_t c;
    ASSERT_EQ_INT(GFS_OK, gfs_create_at(&m.fs, d2, "c", GFS_NODE_DATA, 0, &c));
    ASSERT_EQ_INT(GFS_ENOTEMPTY, gfs_rename(&m.fs, GFS_ROOT_NODE, "d1", GFS_ROOT_NODE, "d2"));
    ASSERT_EQ_INT(GFS_OK, gfs_rename(&m.fs, GFS_ROOT_NODE, "d1", GFS_ROOT_NODE, "d3"));
    ASSERT_EQ_INT(GFS_ENOENT, gfs_node_get(&m.fs, d3, &n)); /* empty dir freed */

    /* Type mismatches. */
    ASSERT_EQ_INT(GFS_ENOTDIR, gfs_rename(&m.fs, GFS_ROOT_NODE, "d3", GFS_ROOT_NODE, "f2"));
    ASSERT_EQ_INT(GFS_EISDIR, gfs_rename(&m.fs, GFS_ROOT_NODE, "f2", GFS_ROOT_NODE, "d3"));

    /* Hard links to one node: rename between them is a no-op. */
    ASSERT_EQ_INT(GFS_OK, gfs_link(&m.fs, GFS_ROOT_NODE, "twin", f1, GFS_EDGE_NAME));
    ASSERT_EQ_INT(GFS_OK, gfs_rename(&m.fs, GFS_ROOT_NODE, "f2", GFS_ROOT_NODE, "twin"));
    ASSERT_EQ_INT(GFS_OK, gfs_resolve(&m.fs, "/f2", &r));
    ASSERT_EQ_INT((long long)f1, (long long)r);
    ASSERT_EQ_INT(GFS_OK, gfs_resolve(&m.fs, "/twin", &r));
    ASSERT_EQ_INT((long long)f1, (long long)r);

    /* Cleanup releases every node and block the test took. */
    ASSERT_EQ_INT(GFS_OK, gfs_unlink(&m.fs, GFS_ROOT_NODE, "twin"));
    ASSERT_EQ_INT(GFS_OK, gfs_unlink(&m.fs, GFS_ROOT_NODE, "f2"));
    ASSERT_EQ_INT(GFS_OK, gfs_unlink(&m.fs, d2, "c"));
    ASSERT_EQ_INT(GFS_OK, gfs_unlink(&m.fs, GFS_ROOT_NODE, "d2"));
    ASSERT_EQ_INT(GFS_OK, gfs_unlink(&m.fs, GFS_ROOT_NODE, "d3")); /* d1, renamed */
    ASSERT_EQ_INT((long long)free_nodes0, (long long)m.fs.free_nodes);
    ASSERT_EQ_INT((long long)free_blocks0, (long long)m.fs.free_blocks);

    ASSERT_EQ_INT(GFS_OK, fsck_clean(&m.fs));
    fs_teardown(&m);
}

TEST(graphfs_rename_no_cycles) {
    struct mounted m;
    fs_setup(&m, 64);

    /* /a/b/c: moving /a under any of its descendants must fail — the
     * subtree would become unreachable. */
    uint64_t a, b, c;
    ASSERT_EQ_INT(GFS_OK, gfs_create_at(&m.fs, GFS_ROOT_NODE, "a", GFS_NODE_NAMESPACE, 0, &a));
    ASSERT_EQ_INT(GFS_OK, gfs_create_at(&m.fs, a, "b", GFS_NODE_NAMESPACE, 0, &b));
    ASSERT_EQ_INT(GFS_OK, gfs_create_at(&m.fs, b, "c", GFS_NODE_NAMESPACE, 0, &c));
    ASSERT_EQ_INT(GFS_EINVAL, gfs_rename(&m.fs, GFS_ROOT_NODE, "a", a, "x"));
    ASSERT_EQ_INT(GFS_EINVAL, gfs_rename(&m.fs, GFS_ROOT_NODE, "a", b, "x"));
    ASSERT_EQ_INT(GFS_EINVAL, gfs_rename(&m.fs, GFS_ROOT_NODE, "a", c, "x"));
    /* A *sibling* subtree is fine. */
    uint64_t d;
    ASSERT_EQ_INT(GFS_OK, gfs_create_at(&m.fs, GFS_ROOT_NODE, "d", GFS_NODE_NAMESPACE, 0, &d));
    ASSERT_EQ_INT(GFS_OK, gfs_rename(&m.fs, GFS_ROOT_NODE, "a", d, "a"));
    uint64_t r;
    ASSERT_EQ_INT(GFS_OK, gfs_resolve(&m.fs, "/d/a/b/c", &r));
    ASSERT_EQ_INT((long long)c, (long long)r);

    ASSERT_EQ_INT(GFS_OK, fsck_clean(&m.fs));
    fs_teardown(&m);
}

TEST(graphfs_truncate) {
    struct mounted m;
    fs_setup(&m, 64);
    uint64_t free_blocks0 = m.fs.free_blocks;

    uint64_t f;
    ASSERT_EQ_INT(GFS_OK, gfs_create_at(&m.fs, GFS_ROOT_NODE, "t", GFS_NODE_DATA, 0644, &f));
    size_t len = (3 * GFS_BLOCK_SIZE) + 100;
    uint8_t *src = malloc(len);
    for (size_t i = 0; i < len; i++) {
        src[i] = (uint8_t)(i | 1); /* never zero */
    }
    ASSERT_EQ_INT((long long)len, gfs_write(&m.fs, f, 0, src, len));

    /* Shrink to mid-block: whole blocks past the end come back free. */
    ASSERT_EQ_INT(GFS_OK, gfs_truncate(&m.fs, f, GFS_BLOCK_SIZE + 10));
    struct gfs_node n;
    ASSERT_EQ_INT(GFS_OK, gfs_node_get(&m.fs, f, &n));
    ASSERT_EQ_INT((long long)(GFS_BLOCK_SIZE + 10), (long long)n.size);
    uint8_t buf[GFS_BLOCK_SIZE];
    ASSERT_EQ_INT(10, gfs_read(&m.fs, f, GFS_BLOCK_SIZE, buf, sizeof(buf)));
    ASSERT_EQ_INT(0, memcmp(buf, src + GFS_BLOCK_SIZE, 10));
    ASSERT_EQ_INT(GFS_OK, fsck_clean(&m.fs)); /* incl. the zero-tail invariant */

    /* Extend past the old end: the bytes between old and new EOF read
     * zero — the shrink zeroed the stale tail. */
    ASSERT_EQ_INT(GFS_OK, gfs_truncate(&m.fs, f, 2ull * GFS_BLOCK_SIZE));
    ASSERT_EQ_INT((long long)(GFS_BLOCK_SIZE - 10),
                  gfs_read(&m.fs, f, GFS_BLOCK_SIZE + 10, buf, sizeof(buf)));
    for (size_t i = 0; i < GFS_BLOCK_SIZE - 10; i++) {
        ASSERT_TRUE(buf[i] == 0);
    }
    /* The first kept bytes are still intact. */
    ASSERT_EQ_INT(10, gfs_read(&m.fs, f, GFS_BLOCK_SIZE, buf, 10));
    ASSERT_EQ_INT(0, memcmp(buf, src + GFS_BLOCK_SIZE, 10));

    /* Truncate to zero releases every data block. */
    ASSERT_EQ_INT(GFS_OK, gfs_truncate(&m.fs, f, 0));
    ASSERT_EQ_INT(GFS_OK, gfs_node_get(&m.fs, f, &n));
    ASSERT_EQ_INT(0, (long long)n.size);
    ASSERT_EQ_INT(0, gfs_read(&m.fs, f, 0, buf, sizeof(buf)));

    /* Error paths, and the full accounting round trip. */
    ASSERT_EQ_INT(GFS_EFBIG, gfs_truncate(&m.fs, f, (512ull * GFS_BLOCK_SIZE) + 1));
    ASSERT_EQ_INT(GFS_EISDIR, gfs_truncate(&m.fs, GFS_ROOT_NODE, 0));
    ASSERT_EQ_INT(GFS_OK, gfs_unlink(&m.fs, GFS_ROOT_NODE, "t"));
    ASSERT_EQ_INT((long long)free_blocks0, (long long)m.fs.free_blocks);

    ASSERT_EQ_INT(GFS_OK, fsck_clean(&m.fs));
    free(src);
    fs_teardown(&m);
}

TEST(graphfs_write_cycle_accounting) {
    struct mounted m;
    fs_setup(&m, 128);
    uint64_t free_nodes0 = m.fs.free_nodes;
    uint64_t free_blocks0 = m.fs.free_blocks;

    /* A miniature of the kernel's boot stress test: repeated
     * create/write/rename/truncate/unlink cycles must return the
     * filesystem to its exact starting occupancy, fsck-clean. */
    for (int round = 0; round < 5; round++) {
        uint64_t d;
        ASSERT_EQ_INT(GFS_OK,
                      gfs_create_at(&m.fs, GFS_ROOT_NODE, "work", GFS_NODE_NAMESPACE, 0755, &d));
        for (int i = 0; i < 6; i++) {
            char name[8];
            snprintf(name, sizeof(name), "f%d", i);
            uint64_t f;
            ASSERT_EQ_INT(GFS_OK, gfs_create_at(&m.fs, d, name, GFS_NODE_DATA, 0644, &f));
            size_t len = ((size_t)(i + 1) * 1500) + 1;
            uint8_t *blob = malloc(len);
            for (size_t j = 0; j < len; j++) {
                blob[j] = (uint8_t)((j * 7) + (size_t)i + 1);
            }
            ASSERT_EQ_INT((long long)len, gfs_write(&m.fs, f, 0, blob, len));
            uint8_t *got = calloc(1, len);
            ASSERT_EQ_INT((long long)len, gfs_read(&m.fs, f, 0, got, len));
            ASSERT_EQ_INT(0, memcmp(blob, got, len));
            free(blob);
            free(got);
        }
        ASSERT_EQ_INT(GFS_OK, gfs_rename(&m.fs, d, "f0", d, "f0r"));
        ASSERT_EQ_INT(GFS_OK, gfs_rename(&m.fs, d, "f1", d, "f2")); /* replace */
        ASSERT_EQ_INT(GFS_EINVAL, gfs_truncate(&m.fs, 0, 0));       /* the null id */
        static const char *const leftovers[] = {"f0r", "f2", "f3", "f4", "f5"};
        for (unsigned i = 0; i < sizeof(leftovers) / sizeof(leftovers[0]); i++) {
            ASSERT_EQ_INT(GFS_OK, gfs_unlink(&m.fs, d, leftovers[i]));
        }
        ASSERT_EQ_INT(GFS_OK, gfs_unlink(&m.fs, GFS_ROOT_NODE, "work"));
        ASSERT_EQ_INT((long long)free_nodes0, (long long)m.fs.free_nodes);
        ASSERT_EQ_INT((long long)free_blocks0, (long long)m.fs.free_blocks);
        ASSERT_EQ_INT(GFS_OK, fsck_clean(&m.fs));
    }
    fs_teardown(&m);
}
