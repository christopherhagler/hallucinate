/*
 * test_heap.c - host unit tests for the slab allocator core
 * (kernel/mm/heap_core.c), with a counting page backend so page leaks
 * and double releases are visible.
 */
#include "test.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <heap_core.h>

static int64_t pages_out; /* pages currently held by the heap */

static void *backend_alloc(void *ctx, size_t count) {
    (void)ctx;
    void *p = aligned_alloc(HEAP_PAGE_SIZE, count * HEAP_PAGE_SIZE);
    if (p != NULL) {
        pages_out += (int64_t)count;
    }
    return p;
}

static void backend_free(void *ctx, void *pages, size_t count) {
    (void)ctx;
    pages_out -= (int64_t)count;
    free(pages);
}

static struct heap fresh_heap(void) {
    struct heap h;
    struct heap_backend b = {backend_alloc, backend_free, NULL};
    heap_core_init(&h, b);
    pages_out = 0;
    return h;
}

TEST(heap_basic_roundtrip) {
    struct heap h = fresh_heap();
    void *p = heap_core_alloc(&h, 100);
    ASSERT_TRUE(p != NULL);
    ASSERT_TRUE(((uintptr_t)p % HEAP_ALIGN) == 0);
    memset(p, 0xAB, 100);
    ASSERT_EQ_INT(1, (int)h.live_objects);
    ASSERT_EQ_INT(0, heap_core_free(&h, p));
    ASSERT_EQ_INT(0, (int)h.live_objects);
    ASSERT_EQ_INT(0, (int)pages_out); /* empty slab returned */
}

TEST(heap_size_zero_returns_null) {
    struct heap h = fresh_heap();
    ASSERT_TRUE(heap_core_alloc(&h, 0) == NULL);
}

TEST(heap_all_classes_and_boundaries) {
    struct heap h = fresh_heap();
    size_t sizes[] = {1, 15, 16, 17, 32, 33, 64, 128, 255, 256, 512, 1000, 1024};
    void *ptrs[sizeof(sizes) / sizeof(sizes[0])];
    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        ptrs[i] = heap_core_alloc(&h, sizes[i]);
        ASSERT_TRUE(ptrs[i] != NULL);
        ASSERT_TRUE(((uintptr_t)ptrs[i] % HEAP_ALIGN) == 0);
        memset(ptrs[i], (int)i + 1, sizes[i]); /* ASan guards the class size */
    }
    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        /* Contents survived neighboring writes. */
        ASSERT_TRUE(((uint8_t *)ptrs[i])[0] == (uint8_t)(i + 1));
        ASSERT_TRUE(((uint8_t *)ptrs[i])[sizes[i] - 1] == (uint8_t)(i + 1));
        ASSERT_EQ_INT(0, heap_core_free(&h, ptrs[i]));
    }
    ASSERT_EQ_INT(0, (int)pages_out);
}

TEST(heap_large_allocations) {
    struct heap h = fresh_heap();
    /* 1025 bytes exceeds the largest class -> whole-page span. */
    void *p = heap_core_alloc(&h, 1025);
    ASSERT_TRUE(p != NULL);
    ASSERT_EQ_INT(1, (int)pages_out);
    memset(p, 0x5A, 1025);
    ASSERT_EQ_INT(0, heap_core_free(&h, p));
    ASSERT_EQ_INT(0, (int)pages_out);

    /* Multi-page span: 3 pages worth of data. */
    size_t big = (3 * HEAP_PAGE_SIZE) - 100;
    uint8_t *q = heap_core_alloc(&h, big);
    ASSERT_TRUE(q != NULL);
    ASSERT_EQ_INT(3, (int)pages_out);
    q[0] = 0x11;
    q[big - 1] = 0x22;
    ASSERT_TRUE(q[0] == 0x11 && q[big - 1] == 0x22);
    ASSERT_EQ_INT(0, heap_core_free(&h, q));
    ASSERT_EQ_INT(0, (int)pages_out);
}

TEST(heap_slab_fill_and_reuse) {
    struct heap h = fresh_heap();
    /* 1024-class slabs hold 3 objects: fill two slabs. */
    void *p[6];
    for (int i = 0; i < 6; i++) {
        p[i] = heap_core_alloc(&h, 1024);
        ASSERT_TRUE(p[i] != NULL);
    }
    ASSERT_EQ_INT(2, (int)pages_out);
    /* Freeing one object must make its slot reusable. */
    ASSERT_EQ_INT(0, heap_core_free(&h, p[2]));
    void *again = heap_core_alloc(&h, 1024);
    ASSERT_TRUE(again == p[2]);
    ASSERT_EQ_INT(2, (int)pages_out);
    for (int i = 0; i < 6; i++) {
        ASSERT_EQ_INT(0, heap_core_free(&h, p[i]));
    }
    ASSERT_EQ_INT(0, (int)pages_out);
    ASSERT_EQ_INT(0, (int)h.live_objects);
}

TEST(heap_rejects_bad_frees) {
    struct heap h = fresh_heap();
    void *p = heap_core_alloc(&h, 64);
    ASSERT_TRUE(p != NULL);
    /* Interior pointer: not an object boundary. */
    ASSERT_EQ_INT(-1, heap_core_free(&h, (uint8_t *)p + 8));
    /* NULL. */
    ASSERT_EQ_INT(-1, heap_core_free(&h, NULL));
    ASSERT_EQ_INT(0, heap_core_free(&h, p));
}

TEST(heap_random_stress_with_shadow) {
    struct heap h = fresh_heap();
    enum { SLOTS = 512 };
    static void *ptr[SLOTS];
    static size_t sz[SLOTS];
    memset((void *)ptr, 0, sizeof(ptr));
    unsigned seed = 12345;
    for (int round = 0; round < 20000; round++) {
        seed = (seed * 1103515245) + 12345;
        int slot = (int)((seed >> 8) % SLOTS);
        if (ptr[slot] == NULL) {
            seed = (seed * 1103515245) + 12345;
            size_t size = 1 + ((seed >> 8) % 3000);
            ptr[slot] = heap_core_alloc(&h, size);
            ASSERT_TRUE(ptr[slot] != NULL);
            sz[slot] = size;
            memset(ptr[slot], slot & 0xFF, size);
        } else {
            uint8_t *b = ptr[slot];
            ASSERT_TRUE(b[0] == (uint8_t)(slot & 0xFF));
            ASSERT_TRUE(b[sz[slot] - 1] == (uint8_t)(slot & 0xFF));
            ASSERT_EQ_INT(0, heap_core_free(&h, ptr[slot]));
            ptr[slot] = NULL;
        }
    }
    for (int i = 0; i < SLOTS; i++) {
        if (ptr[i] != NULL) {
            ASSERT_EQ_INT(0, heap_core_free(&h, ptr[i]));
        }
    }
    ASSERT_EQ_INT(0, (int)h.live_objects);
    ASSERT_EQ_INT(0, (int)pages_out);
}
