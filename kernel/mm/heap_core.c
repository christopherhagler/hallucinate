/*
 * heap_core.c - slab allocator core (pure logic, host-testable).
 */
#include <heap_core.h>

#include <string.h>

#define SLAB_MAGIC  0x48534C42u /* "HSLB" */
#define LARGE_MAGIC 0x484C4741u /* "HLGA" */
#define HDR_SIZE    64u

struct slab_hdr {
    uint32_t magic;
    uint16_t class_idx;    /* small slabs */
    uint16_t inuse;        /* small slabs: live objects in this slab */
    void *free_list;       /* small slabs: first free object */
    struct slab_hdr *next; /* partial-list linkage */
    struct slab_hdr *prev;
    uint64_t npages; /* large spans */
};

_Static_assert(sizeof(struct slab_hdr) <= HDR_SIZE, "slab header must fit its slot");

static const uint16_t class_size[HEAP_NUM_CLASSES] = {16, 32, 64, 128, 256, 512, 1024};

struct free_obj {
    void *next;
};

static int class_for(size_t size) {
    for (int i = 0; i < HEAP_NUM_CLASSES; i++) {
        if (size <= class_size[i]) {
            return i;
        }
    }
    return -1;
}

static uint16_t objects_per_slab(int cls) {
    return (uint16_t)((HEAP_PAGE_SIZE - HDR_SIZE) / class_size[cls]);
}

static void partial_insert(struct heap *h, int cls, struct slab_hdr *s) {
    s->prev = NULL;
    s->next = h->partial[cls];
    if (s->next != NULL) {
        s->next->prev = s;
    }
    h->partial[cls] = s;
}

static void partial_remove(struct heap *h, int cls, struct slab_hdr *s) {
    if (s->prev != NULL) {
        s->prev->next = s->next;
    } else {
        h->partial[cls] = s->next;
    }
    if (s->next != NULL) {
        s->next->prev = s->prev;
    }
    s->next = NULL;
    s->prev = NULL;
}

static struct slab_hdr *slab_create(struct heap *h, int cls) {
    struct slab_hdr *s = h->backend.pages_alloc(h->backend.ctx, 1);
    if (s == NULL) {
        return NULL;
    }
    h->live_pages++;
    memset(s, 0, HDR_SIZE);
    s->magic = SLAB_MAGIC;
    s->class_idx = (uint16_t)cls;

    /* Chain every object into the free list, first object first. */
    uint8_t *base = (uint8_t *)s + HDR_SIZE;
    uint16_t count = objects_per_slab(cls);
    for (int i = count - 1; i >= 0; i--) {
        struct free_obj *obj = (struct free_obj *)(base + ((size_t)i * class_size[cls]));
        obj->next = s->free_list;
        s->free_list = obj;
    }
    return s;
}

void heap_core_init(struct heap *h, struct heap_backend backend) {
    memset(h, 0, sizeof(*h));
    h->backend = backend;
}

static void *alloc_large(struct heap *h, size_t size) {
    size_t npages = (size + HDR_SIZE + HEAP_PAGE_SIZE - 1) / HEAP_PAGE_SIZE;
    struct slab_hdr *s = h->backend.pages_alloc(h->backend.ctx, npages);
    if (s == NULL) {
        return NULL;
    }
    h->live_pages += npages;
    memset(s, 0, HDR_SIZE);
    s->magic = LARGE_MAGIC;
    s->npages = npages;
    return (uint8_t *)s + HDR_SIZE;
}

void *heap_core_alloc(struct heap *h, size_t size) {
    if (size == 0) {
        return NULL;
    }
    if (size > HEAP_MAX_SMALL) {
        return alloc_large(h, size);
    }

    int cls = class_for(size);
    struct slab_hdr *s = h->partial[cls];
    if (s == NULL) {
        s = slab_create(h, cls);
        if (s == NULL) {
            return NULL;
        }
        partial_insert(h, cls, s);
    }

    struct free_obj *obj = s->free_list;
    s->free_list = obj->next;
    s->inuse++;
    h->live_objects++;
    if (s->free_list == NULL) {
        partial_remove(h, cls, s); /* slab is now full */
    }
    return obj;
}

int heap_core_free(struct heap *h, void *ptr) {
    if (ptr == NULL) {
        return -1;
    }
    struct slab_hdr *s = (struct slab_hdr *)((uintptr_t)ptr & ~(uintptr_t)(HEAP_PAGE_SIZE - 1));

    if (s->magic == LARGE_MAGIC) {
        if (ptr != (uint8_t *)s + HDR_SIZE) {
            return -1;
        }
        size_t npages = s->npages;
        s->magic = 0; /* poison against double free of the same span */
        h->backend.pages_free(h->backend.ctx, s, npages);
        h->live_pages -= npages;
        return 0;
    }

    if (s->magic != SLAB_MAGIC || s->class_idx >= HEAP_NUM_CLASSES) {
        return -1;
    }
    uint16_t size = class_size[s->class_idx];
    uintptr_t offset = (uintptr_t)ptr - ((uintptr_t)s + HDR_SIZE);
    if (offset % size != 0 || offset / size >= objects_per_slab(s->class_idx) || s->inuse == 0) {
        return -1;
    }

    int was_full = (s->free_list == NULL);
    struct free_obj *obj = ptr;
    obj->next = s->free_list;
    s->free_list = obj;
    s->inuse--;
    h->live_objects--;

    if (s->inuse == 0) {
        if (!was_full) {
            partial_remove(h, s->class_idx, s);
        }
        s->magic = 0;
        h->backend.pages_free(h->backend.ctx, s, 1);
        h->live_pages--;
    } else if (was_full) {
        partial_insert(h, s->class_idx, s);
    }
    return 0;
}
