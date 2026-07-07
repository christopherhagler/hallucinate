/*
 * block.c - block device registry and write-through block cache.
 *
 * The cache holds CACHE_ENTRIES frames keyed by lba, replaced LRU.
 * Reads fill the cache; writes update it and go straight to the
 * driver (write-through), so the cache never holds dirty data and a
 * crash can only lose what the filesystem itself had not ordered.
 * fsync-driven flushing arrives with the write path (Phase 5d).
 */
#include <block.h>

#include <stddef.h>

#include <errno.h>
#include <kprintf.h>
#include <memlayout.h>
#include <panic.h>
#include <pmm.h>
#include <string.h>

#define CACHE_ENTRIES 64

struct cache_entry {
    uint64_t lba;
    uint64_t stamp; /* LRU clock; 0 = empty */
    uint8_t *data;  /* one frame via phys_to_virt */
};

static struct bdev *root_bdev;
static struct cache_entry cache[CACHE_ENTRIES];
static uint64_t lru_clock;
static int busy; /* reentry guard, see block.h contract */

void block_register(struct bdev *bd) {
    KASSERT(bd != NULL && bd->read != NULL && bd->write != NULL && bd->nblocks > 0);
    if (root_bdev != NULL) {
        panic("block: second device (%s) — multi-disk support is future work", bd->name);
    }
    for (int i = 0; i < CACHE_ENTRIES; i++) {
        uint64_t frame = pmm_alloc_frame();
        if (frame == 0) {
            panic("block: out of frames for the cache");
        }
        cache[i].data = phys_to_virt(frame);
        cache[i].stamp = 0;
    }
    root_bdev = bd;
    kprintf("block: %s, %llu MiB (%llu blocks of %u), cache %u KiB\n", bd->name,
            (unsigned long long)(bd->nblocks * BLOCK_SIZE >> 20), (unsigned long long)bd->nblocks,
            BLOCK_SIZE, CACHE_ENTRIES * BLOCK_SIZE / 1024);
}

struct bdev *block_root(void) {
    return root_bdev;
}

static struct cache_entry *cache_lookup(uint64_t lba) {
    for (int i = 0; i < CACHE_ENTRIES; i++) {
        if (cache[i].stamp != 0 && cache[i].lba == lba) {
            return &cache[i];
        }
    }
    return NULL;
}

static struct cache_entry *cache_victim(void) {
    struct cache_entry *v = &cache[0];
    for (int i = 1; i < CACHE_ENTRIES; i++) {
        if (cache[i].stamp < v->stamp) {
            v = &cache[i];
        }
    }
    return v;
}

static void enter(struct bdev *bd, uint64_t lba) {
    KASSERT(bd == root_bdev && lba < bd->nblocks);
    KASSERT(!busy); /* v1: single-caller layer, see header */
    busy = 1;
}

int block_read(struct bdev *bd, uint64_t lba, void *buf) {
    if (bd == NULL || lba >= bd->nblocks) {
        return -EINVAL;
    }
    enter(bd, lba);
    struct cache_entry *e = cache_lookup(lba);
    if (e == NULL) {
        e = cache_victim();
        int rc = bd->read(bd, lba, e->data);
        if (rc != 0) {
            e->stamp = 0; /* do not cache a failed fill */
            busy = 0;
            return rc;
        }
        e->lba = lba;
    }
    e->stamp = ++lru_clock;
    memcpy(buf, e->data, BLOCK_SIZE);
    busy = 0;
    return 0;
}

int block_write(struct bdev *bd, uint64_t lba, const void *buf) {
    if (bd == NULL || lba >= bd->nblocks) {
        return -EINVAL;
    }
    enter(bd, lba);
    struct cache_entry *e = cache_lookup(lba);
    if (e == NULL) {
        e = cache_victim();
        e->lba = lba;
    }
    memcpy(e->data, buf, BLOCK_SIZE);
    e->stamp = ++lru_clock;
    int rc = bd->write(bd, lba, e->data);
    if (rc != 0) {
        e->stamp = 0; /* device state unknown; drop the entry */
    }
    busy = 0;
    return rc;
}

void block_selftest(void) {
    struct bdev *bd = root_bdev;
    if (bd == NULL) {
        kprintf("block: selftest skipped (no device)\n");
        return;
    }
    uint64_t lba = bd->nblocks - 1;
    uint64_t save_phys = pmm_alloc_frame();
    uint64_t work_phys = pmm_alloc_frame();
    KASSERT(save_phys != 0 && work_phys != 0);
    uint8_t *save = phys_to_virt(save_phys);
    uint8_t *work = phys_to_virt(work_phys);

    /* Round-trip through the raw driver ops (the cache would satisfy
     * a read after write without ever touching the device). */
    if (bd->read(bd, lba, save) != 0) {
        panic("block: selftest read failed");
    }
    for (unsigned i = 0; i < BLOCK_SIZE; i++) {
        work[i] = (uint8_t)((i * 7) + 3);
    }
    if (bd->write(bd, lba, work) != 0) {
        panic("block: selftest write failed");
    }
    memset(work, 0, BLOCK_SIZE);
    if (bd->read(bd, lba, work) != 0) {
        panic("block: selftest readback failed");
    }
    for (unsigned i = 0; i < BLOCK_SIZE; i++) {
        if (work[i] != (uint8_t)((i * 7) + 3)) {
            panic("block: readback mismatch at byte %u", i);
        }
    }
    /* Leave the device exactly as found, then verify the cached path
     * returns the restored contents. */
    if (bd->write(bd, lba, save) != 0) {
        panic("block: selftest restore failed");
    }
    if (block_read(bd, lba, work) != 0 || memcmp(work, save, BLOCK_SIZE) != 0) {
        panic("block: cached read disagrees with the device");
    }
    pmm_free_frame(save_phys);
    pmm_free_frame(work_phys);
    kprintf("block: selftest passed (write/readback/restore)\n");
}
