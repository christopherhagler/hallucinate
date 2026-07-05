/*
 * pmm_core.c - bitmap frame allocator (pure logic, host-testable).
 */
#include <pmm_core.h>

#include <string.h>

#define BITS_PER_BYTE 8

static int test_bit(const struct pmm *p, uint64_t frame) {
    return (p->bitmap[frame / BITS_PER_BYTE] >> (frame % BITS_PER_BYTE)) & 1;
}

static void set_bit(struct pmm *p, uint64_t frame) {
    p->bitmap[frame / BITS_PER_BYTE] |= (uint8_t)(1u << (frame % BITS_PER_BYTE));
}

static void clear_bit(struct pmm *p, uint64_t frame) {
    p->bitmap[frame / BITS_PER_BYTE] &= (uint8_t)~(1u << (frame % BITS_PER_BYTE));
}

uint64_t pmm_core_bitmap_size(uint64_t nframes) {
    return (nframes + BITS_PER_BYTE - 1) / BITS_PER_BYTE;
}

void pmm_core_init(struct pmm *p, uint8_t *bitmap, uint64_t nframes) {
    p->bitmap = bitmap;
    p->nframes = nframes;
    p->free_frames = 0;
    p->hint = 0;
    memset(bitmap, 0xFF, pmm_core_bitmap_size(nframes));
}

void pmm_core_mark_used(struct pmm *p, uint64_t first, uint64_t count) {
    for (uint64_t f = first; f < first + count && f < p->nframes; f++) {
        if (!test_bit(p, f)) {
            set_bit(p, f);
            p->free_frames--;
        }
    }
}

void pmm_core_mark_free(struct pmm *p, uint64_t first, uint64_t count) {
    for (uint64_t f = first; f < first + count && f < p->nframes; f++) {
        if (test_bit(p, f)) {
            clear_bit(p, f);
            p->free_frames++;
        }
    }
}

int pmm_core_is_free(const struct pmm *p, uint64_t frame) {
    if (frame >= p->nframes) {
        return 0;
    }
    return !test_bit(p, frame);
}

int64_t pmm_core_alloc(struct pmm *p) {
    if (p->free_frames == 0) {
        return -1;
    }
    /* Next-fit: resume where the last search ended, wrap once. */
    for (uint64_t scanned = 0; scanned < p->nframes; scanned++) {
        uint64_t f = (p->hint + scanned) % p->nframes;
        /* Fast-skip fully used bytes at byte boundaries. */
        if (f % BITS_PER_BYTE == 0 && p->bitmap[f / BITS_PER_BYTE] == 0xFF &&
            f + BITS_PER_BYTE <= p->nframes) {
            scanned += BITS_PER_BYTE - 1;
            continue;
        }
        if (!test_bit(p, f)) {
            set_bit(p, f);
            p->free_frames--;
            p->hint = (f + 1) % p->nframes;
            return (int64_t)f;
        }
    }
    return -1; /* unreachable while free_frames is exact */
}

int64_t pmm_core_alloc_run(struct pmm *p, uint64_t count, uint64_t align) {
    if (count == 0 || align == 0 || (align & (align - 1)) != 0 || count > p->free_frames) {
        return -1;
    }
    for (uint64_t base = 0; base + count <= p->nframes; base += align) {
        uint64_t run = 0;
        while (run < count && !test_bit(p, base + run)) {
            run++;
        }
        if (run == count) {
            pmm_core_mark_used(p, base, count);
            return (int64_t)base;
        }
    }
    return -1;
}

int pmm_core_free(struct pmm *p, uint64_t frame) {
    if (frame >= p->nframes || !test_bit(p, frame)) {
        return -1;
    }
    clear_bit(p, frame);
    p->free_frames++;
    return 0;
}
