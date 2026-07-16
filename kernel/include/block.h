/*
 * block.h - block device layer.
 *
 * A block device presents an array of 4 KiB blocks (the filesystem
 * block size; drivers translate to their native sector size). All
 * access goes through block_read/block_write, which maintain a small
 * write-through cache of recently used blocks.
 *
 * Concurrency contract: one caller at a time, asserted against
 * reentry. I/O is synchronous and can take milliseconds, so callers
 * must NOT hold interrupts off. The contract is discharged by the
 * VFS: every runtime disk path goes through vfs.c, which serializes
 * behind one sleeping mutex; boot-time callers (block_selftest, the
 * mount) run before userspace exists.
 */
#pragma once

#include <stdint.h>

#define BLOCK_SIZE 4096u

struct bdev {
    const char *name;
    uint64_t nblocks; /* device size in 4 KiB blocks */
    /* Driver ops: buf is BLOCK_SIZE bytes, physically contiguous.
     * Return 0 or -EIO. */
    int (*read)(struct bdev *bd, uint64_t lba, void *buf);
    int (*write)(struct bdev *bd, uint64_t lba, const void *buf);
    void *priv;
};

/* Register the (single, v1) block device; called by the driver. */
void block_register(struct bdev *bd);

/* The registered device, or NULL when the machine has none. */
struct bdev *block_root(void);

/* Cached access. buf is BLOCK_SIZE bytes, any kernel memory.
 * Returns 0, -EIO (driver failure) or -EINVAL (lba out of range). */
int block_read(struct bdev *bd, uint64_t lba, void *buf);
int block_write(struct bdev *bd, uint64_t lba, const void *buf);

/* Boot-time verification: write/readback/restore on the last block.
 * Panics on failure; no-op message when no device is present. */
void block_selftest(void);
