/*
 * heap_core.h - slab allocator core.
 *
 * Pure logic against a page-provider backend: no kernel dependencies,
 * unit-tested on the host under sanitizers (tests/host/test_heap.c).
 * The kernel wrapper (kernel/mm/kmalloc.c) supplies pages from the
 * PMM through the HHDM and turns error returns into panics.
 *
 * Design:
 *   - size classes 16..1024 bytes; each slab is one page whose first
 *     64 bytes are the slab header, objects linked through a
 *     free list; partially-full slabs sit on a per-class list
 *   - larger requests take whole contiguous pages ("large" spans)
 *     with the same header at the span start
 *   - kfree finds the header by masking the pointer to the page
 *     base, so slab pages must be page-aligned in the address space
 *     the caller uses
 *   - empty slabs are returned to the backend immediately
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#define HEAP_PAGE_SIZE   4096u
#define HEAP_ALIGN       16u
#define HEAP_MAX_SMALL   1024u
#define HEAP_NUM_CLASSES 7 /* 16 32 64 128 256 512 1024 */

struct heap_backend {
    /* Allocate `count` contiguous, page-aligned pages; NULL if
     * exhausted. `count` is 1 for slab pages. */
    void *(*pages_alloc)(void *ctx, size_t count);
    void (*pages_free)(void *ctx, void *pages, size_t count);
    void *ctx;
};

struct slab_hdr; /* opaque outside heap_core.c */

struct heap {
    struct heap_backend backend;
    struct slab_hdr *partial[HEAP_NUM_CLASSES];
    /* Statistics. */
    uint64_t live_objects; /* small objects currently allocated */
    uint64_t live_pages;   /* pages currently held from the backend */
};

void heap_core_init(struct heap *h, struct heap_backend backend);

/* NULL on size 0 or backend exhaustion. Result is 16-byte aligned. */
void *heap_core_alloc(struct heap *h, size_t size);

/*
 * Free a pointer previously returned by heap_core_alloc.
 * Returns 0, or -1 if the pointer is not a live allocation (wrong
 * address, double free of an empty slab, corrupted header); the heap
 * is unchanged in that case.
 */
int heap_core_free(struct heap *h, void *ptr);
