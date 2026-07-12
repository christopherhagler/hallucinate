/*
 * graphfs_fsck - verify a graphfs image against the format invariants.
 *
 * A thin driver over the kernel-tested core's gfs_fsck(): checksums the
 * whole metadata tree, cross-checks nlink and single-parent namespaces,
 * and reconciles the reachable block set against the on-disk allocation
 * bitmap. Because graphfs is copy-on-write and self-checksumming, a
 * healthy image always passes; a failure is a real defect. Exit status is
 * 0 for clean, 1 for corrupt, 2 for usage/IO problems.
 *
 *   graphfs_fsck fs.img
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <graphfs_core.h>

struct imgdev {
    FILE *f;
    uint64_t nblk;
};

static int img_read(void *ctx, uint64_t lba, void *buf) {
    struct imgdev *d = ctx;
    if (fseeko(d->f, (off_t)(lba * GFS_BLOCK_SIZE), SEEK_SET) != 0) {
        return -1;
    }
    return fread(buf, 1, GFS_BLOCK_SIZE, d->f) == GFS_BLOCK_SIZE ? 0 : -1;
}

static int img_write(void *ctx, uint64_t lba, const void *buf) {
    struct imgdev *d = ctx;
    if (fseeko(d->f, (off_t)(lba * GFS_BLOCK_SIZE), SEEK_SET) != 0) {
        return -1;
    }
    return fwrite(buf, 1, GFS_BLOCK_SIZE, d->f) == GFS_BLOCK_SIZE ? 0 : -1;
}

static const struct gfs_ops IMG_OPS = {img_read, img_write};

static int g_reports;

static void report(void *cookie, const char *msg) {
    (void)cookie;
    fprintf(stderr, "  fsck: %s\n", msg);
    g_reports++;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: graphfs_fsck FILE\n");
        return 2;
    }
    FILE *f = fopen(argv[1], "rb");
    if (!f) {
        fprintf(stderr, "graphfs_fsck: cannot open %s\n", argv[1]);
        return 2;
    }
    fseeko(f, 0, SEEK_END);
    off_t bytes = ftello(f);
    fseeko(f, 0, SEEK_SET);
    struct imgdev dev = {f, (uint64_t)bytes / GFS_BLOCK_SIZE};

    struct gfs fs;
    int rc = gfs_mount(&fs, &IMG_OPS, &dev, 0, NULL, 0);
    if (rc != GFS_OK) {
        fprintf(stderr, "graphfs_fsck: mount failed: %s\n", gfs_strerror(rc));
        fclose(f);
        return 1;
    }

    size_t wl = gfs_fsck_work_size(fs.node_count, fs.total_blocks);
    void *work = malloc(wl);
    rc = gfs_fsck(&fs, work, wl, report, NULL);
    free(work);
    fclose(f);

    if (rc == GFS_OK) {
        printf("graphfs_fsck: %s clean (gen %llu, %llu/%llu blocks free)\n", argv[1],
               (unsigned long long)fs.generation, (unsigned long long)fs.free_blocks,
               (unsigned long long)fs.total_blocks);
        return 0;
    }
    fprintf(stderr, "graphfs_fsck: %s CORRUPT (%d problems)\n", argv[1], g_reports);
    return 1;
}
