/*
 * pmm_core.h - physical frame allocator core.
 *
 * Pure bitmap bookkeeping over frame indices: no I/O, no panics, no
 * kernel dependencies, so the identical code is unit-tested on the
 * host under sanitizers (tests/host/test_pmm.c). The kernel wrapper
 * (kernel/mm/pmm.c) translates physical addresses to frame indices,
 * places the bitmap, and turns error returns into panics.
 *
 * Convention: a set bit means the frame is USED. Frames start used;
 * init marks everything allocated and the caller frees what the
 * memory map says is usable.
 */
#pragma once

#include <stdint.h>

struct pmm {
    uint8_t *bitmap;      /* nframes bits, rounded up to whole bytes */
    uint64_t nframes;     /* frames under management */
    uint64_t free_frames; /* current number of free frames */
    uint64_t hint;        /* next-fit search position */
};

/* Bytes of bitmap storage required to manage nframes frames. */
uint64_t pmm_core_bitmap_size(uint64_t nframes);

/* Take ownership of the (uninitialized) bitmap; all frames start used. */
void pmm_core_init(struct pmm *p, uint8_t *bitmap, uint64_t nframes);

/*
 * Range marking, used while building the initial map. Both are
 * idempotent and clip to nframes: marking an already-used frame used
 * (or free frame free) is a no-op, and the free count stays exact.
 */
void pmm_core_mark_used(struct pmm *p, uint64_t first, uint64_t count);
void pmm_core_mark_free(struct pmm *p, uint64_t first, uint64_t count);

/* 1 if the frame is free, 0 if used or out of range. */
int pmm_core_is_free(const struct pmm *p, uint64_t frame);

/* Allocate one frame; returns its index or -1 if none are free. */
int64_t pmm_core_alloc(struct pmm *p);

/*
 * Allocate `count` physically contiguous frames whose first frame is
 * aligned to `align` frames (a power of two; 1 = no constraint).
 * Returns the first frame index or -1.
 */
int64_t pmm_core_alloc_run(struct pmm *p, uint64_t count, uint64_t align);

/*
 * Free one previously allocated frame.
 * Returns 0, or -1 if the frame was out of range or already free
 * (double free) — the frame's state is unchanged in that case.
 */
int pmm_core_free(struct pmm *p, uint64_t frame);
