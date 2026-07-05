/*
 * kmalloc.c - global kernel heap: heap_core over PMM frames via the
 * HHDM (every frame's virtual address is page-aligned there, which
 * heap_core's pointer-to-header masking requires).
 */
#include <kmalloc.h>

#include <heap_core.h>
#include <memlayout.h>
#include <panic.h>
#include <pmm.h>
#include <string.h>

static struct heap heap;

static void *backend_pages_alloc(void *ctx, size_t count) {
    (void)ctx;
    uint64_t phys = pmm_alloc_frames(count, 1);
    if (phys == 0) {
        return NULL;
    }
    return phys_to_virt(phys);
}

static void backend_pages_free(void *ctx, void *pages, size_t count) {
    (void)ctx;
    uint64_t phys = virt_to_phys(pages);
    for (size_t i = 0; i < count; i++) {
        pmm_free_frame(phys + (i * PAGE_SIZE));
    }
}

void kmalloc_init(void) {
    struct heap_backend backend = {
        .pages_alloc = backend_pages_alloc,
        .pages_free = backend_pages_free,
        .ctx = NULL,
    };
    heap_core_init(&heap, backend);
}

void *kmalloc(size_t size) {
    return heap_core_alloc(&heap, size);
}

void *kzalloc(size_t size) {
    void *p = heap_core_alloc(&heap, size);
    if (p != NULL) {
        memset(p, 0, size);
    }
    return p;
}

void kfree(void *ptr) {
    if (ptr == NULL) {
        return;
    }
    if (heap_core_free(&heap, ptr) != 0) {
        panic("kfree: invalid or double free of %p", ptr);
    }
}

uint64_t kmalloc_live_objects(void) {
    return heap.live_objects;
}

uint64_t kmalloc_live_pages(void) {
    return heap.live_pages;
}
