/*
 * graphfs_mkfs - build a graphfs image from host files.
 *
 * A thin driver over the exact same kernel-tested core (graphfs_core.c):
 * the image the kernel mounts is produced by the same format code that is
 * verified under ASan/UBSan, so there is no second, drift-prone
 * implementation of the on-disk layout.
 *
 *   graphfs_mkfs --out fs.img --size-mib 16 [--node-count 1024] \
 *                [--dir /dev]... \
 *                /bin/init=build/user/init.elf /bin/hello=build/user/hello.elf
 *
 * Each DEST=SRC installs a host file at an absolute graphfs path, creating
 * intermediate namespaces (mkdir -p) as needed. Each --dir creates an empty
 * namespace (mount points, like /dev, must exist on disk).
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static void die(const char *msg, int rc) {
    if (rc != 0) {
        fprintf(stderr, "graphfs_mkfs: %s: %s\n", msg, gfs_strerror(rc));
    } else {
        fprintf(stderr, "graphfs_mkfs: %s\n", msg);
    }
    exit(1);
}

/* Ensure namespace `name` exists under `dir`, returning its id. */
static uint64_t ensure_dir(struct gfs *fs, uint64_t dir, const char *name) {
    uint64_t id;
    int rc = gfs_lookup(fs, dir, name, &id);
    if (rc == GFS_OK) {
        return id;
    }
    if (rc != GFS_ENOENT) {
        die("lookup", rc);
    }
    rc = gfs_create_at(fs, dir, name, GFS_NODE_NAMESPACE, 0755, &id);
    if (rc != GFS_OK) {
        die("mkdir", rc);
    }
    return id;
}

/* mkdir -p: create every component of an absolute path. */
static void mkdirs(struct gfs *fs, const char *path) {
    if (path[0] != '/') {
        die("directory must be absolute", 0);
    }
    uint64_t dir = GFS_ROOT_NODE;
    const char *p = path + 1;
    char comp[GFS_NAME_MAX + 1];
    while (*p != '\0') {
        size_t n = 0;
        while (p[n] != '\0' && p[n] != '/') {
            n++;
        }
        if (n == 0 || n > GFS_NAME_MAX) {
            die("bad path component", 0);
        }
        memcpy(comp, p, n);
        comp[n] = '\0';
        p += n;
        while (*p == '/') {
            p++;
        }
        dir = ensure_dir(fs, dir, comp);
    }
    printf("  %s (namespace)\n", path);
}

static void install(struct gfs *fs, const char *dest, const char *src) {
    FILE *sf = fopen(src, "rb");
    if (!sf) {
        die(src, 0);
    }
    fseeko(sf, 0, SEEK_END);
    long sz = ftello(sf);
    fseeko(sf, 0, SEEK_SET);
    uint8_t *buf = malloc(sz > 0 ? (size_t)sz : 1);
    if (sz > 0 && fread(buf, 1, (size_t)sz, sf) != (size_t)sz) {
        die(src, 0);
    }
    fclose(sf);

    if (dest[0] != '/') {
        die("destination must be absolute", 0);
    }
    /* Walk components, creating namespaces for all but the last. */
    uint64_t dir = GFS_ROOT_NODE;
    const char *p = dest + 1;
    char comp[GFS_NAME_MAX + 1];
    while (*p != '\0') {
        size_t n = 0;
        while (p[n] != '\0' && p[n] != '/') {
            n++;
        }
        if (n == 0 || n > GFS_NAME_MAX) {
            die("bad path component", 0);
        }
        memcpy(comp, p, n);
        comp[n] = '\0';
        int last = p[n] == '\0';
        p += n;
        while (*p == '/') {
            p++;
        }
        if (last) {
            uint64_t file;
            int rc = gfs_create_at(fs, dir, comp, GFS_NODE_DATA, 0755, &file);
            if (rc != GFS_OK) {
                die("create file", rc);
            }
            if (sz > 0) {
                long w = gfs_write(fs, file, 0, buf, (size_t)sz);
                if (w != sz) {
                    die("write file", (int)w);
                }
            }
            printf("  %s <- %s (%ld bytes)\n", dest, src, sz);
        } else {
            dir = ensure_dir(fs, dir, comp);
        }
    }
    free(buf);
}

#define MKFS_MAX_DIRS 16

int main(int argc, char **argv) {
    const char *out = NULL;
    uint64_t size_mib = 16;
    uint64_t node_count = 1024;
    int first_install = 0;
    const char *dirs[MKFS_MAX_DIRS];
    int ndirs = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
            out = argv[++i];
        } else if (strcmp(argv[i], "--size-mib") == 0 && i + 1 < argc) {
            size_mib = strtoull(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--node-count") == 0 && i + 1 < argc) {
            node_count = strtoull(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--dir") == 0 && i + 1 < argc) {
            if (ndirs == MKFS_MAX_DIRS) {
                die("too many --dir entries", 0);
            }
            dirs[ndirs++] = argv[++i];
        } else {
            first_install = i;
            break;
        }
    }
    if (!out) {
        die("usage: graphfs_mkfs --out FILE --size-mib N [--node-count N] "
            "[--dir PATH]... DEST=SRC...",
            0);
    }

    uint64_t nblk = size_mib * (1024 * 1024 / GFS_BLOCK_SIZE);
    FILE *f = fopen(out, "wb+");
    if (!f) {
        die(out, 0);
    }
    /* Materialize a zero-filled image of the requested size. */
    if (fseeko(f, (off_t)((nblk * GFS_BLOCK_SIZE) - 1), SEEK_SET) != 0 || fputc(0, f) == EOF) {
        die("size image", 0);
    }

    struct imgdev dev = {f, nblk};
    int rc = gfs_mkfs(&IMG_OPS, &dev, nblk, node_count);
    if (rc != GFS_OK) {
        die("mkfs", rc);
    }

    struct gfs fs;
    size_t wl = gfs_mount_work_size(nblk);
    void *work = malloc(wl);
    rc = gfs_mount(&fs, &IMG_OPS, &dev, 1, work, wl);
    if (rc != GFS_OK) {
        die("mount", rc);
    }

    printf("graphfs_mkfs: %s, %llu MiB (%llu blocks), %llu nodes\n", out,
           (unsigned long long)size_mib, (unsigned long long)nblk, (unsigned long long)node_count);
    for (int i = 0; i < ndirs; i++) {
        mkdirs(&fs, dirs[i]);
    }
    if (first_install != 0) {
        for (int i = first_install; i < argc; i++) {
            char *eq = argv[i];
            while (*eq != '\0' && *eq != '=') {
                eq++;
            }
            if (*eq != '=') {
                die("install spec must be DEST=SRC", 0);
            }
            *eq = '\0';
            install(&fs, argv[i], eq + 1);
        }
    }

    gfs_unmount(&fs);
    free(work);
    if (fclose(f) != 0) {
        die("close image", 0);
    }
    return 0;
}
