/*
 * pmm.h - physical memory manager (kernel interface).
 *
 * Hands out 4 KiB physical frames from the E820-usable memory that
 * survives boot reservations (low 1 MiB, kernel image, the allocator
 * bitmap). Until the kernel VMM installs a full physical map, callers
 * may only touch frames below BOOT_MAPPED_LIMIT via phys_to_virt();
 * the allocator itself never dereferences allocated frames.
 */
#pragma once

#include <stdint.h>

#include <bootinfo.h>

#define PAGE_SIZE  4096u
#define PAGE_SHIFT 12

/* Build the frame map from the E820 table. Panics if no memory fits. */
void pmm_init(const struct bootinfo *bi);

/*
 * Allocate one frame; returns its physical address, or 0 when out of
 * memory (physical page 0 lies in the always-reserved low 1 MiB, so
 * 0 is never a valid allocation).
 */
uint64_t pmm_alloc_frame(void);

/* Allocate `count` contiguous frames, first frame aligned to
 * `align_frames` frames (power of two). 0 on failure. */
uint64_t pmm_alloc_frames(uint64_t count, uint64_t align_frames);

/* Return a frame; freeing an unallocated address is a panic. */
void pmm_free_frame(uint64_t paddr);

/* Statistics, in frames. */
uint64_t pmm_total_frames(void);
uint64_t pmm_free_frames(void);

/*
 * Re-derive the bitmap pointer through the current direct map. Called
 * by vmm_init() immediately after it moves hhdm_base; the pointer
 * handed to pmm_init() is stale from that moment on.
 */
void pmm_rebase(void);
