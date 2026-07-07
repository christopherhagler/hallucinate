/*
 * virtq_core.h - VIRTIO 1.2 split virtqueue, pure ring bookkeeping.
 *
 * Plain C data and logic only (no kernel dependencies, no MMIO, no
 * barriers) so the descriptor accounting — the error-prone part of a
 * virtio driver — is unit-tested on the host
 * (tests/host/test_virtq.c). The kernel side (drivers/virtio_blk.c)
 * supplies memory, physical addresses, memory barriers, and device
 * notification.
 *
 * Layout per VIRTIO 1.2 §2.7: a descriptor table, the driver
 * ("available") ring, and the device ("used") ring. The caller
 * allocates the three areas with the required alignment (16/2/4)
 * and hands virtq_init() their virtual addresses.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#define VIRTQ_DESC_F_NEXT  1u /* chained: `next` is valid */
#define VIRTQ_DESC_F_WRITE 2u /* device writes this buffer */

struct virtq_desc {
    uint64_t addr; /* physical */
    uint32_t len;
    uint16_t flags;
    uint16_t next;
};

struct virtq_avail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[]; /* qsize entries */
};

struct virtq_used_elem {
    uint32_t id; /* head of the completed descriptor chain */
    uint32_t len;
};

struct virtq_used {
    uint16_t flags;
    uint16_t idx;
    struct virtq_used_elem ring[]; /* qsize entries */
};

/* One buffer of a request: physical address + length. */
struct virtq_sg {
    uint64_t addr;
    uint32_t len;
};

struct virtq {
    uint16_t size;
    struct virtq_desc *desc;
    struct virtq_avail *avail;
    struct virtq_used *used;
    uint16_t free_head; /* head of the free-descriptor list */
    uint16_t num_free;
    uint16_t last_used; /* next used->ring slot to consume */
};

/* Byte sizes of the three areas for a queue of `size` entries. */
size_t virtq_desc_bytes(uint16_t size);
size_t virtq_avail_bytes(uint16_t size);
size_t virtq_used_bytes(uint16_t size);

/*
 * Bind the ring memory (zeroed by the caller) and chain every
 * descriptor onto the free list. `size` must be a power of two
 * (VIRTIO requires it), at least 2.
 */
void virtq_init(struct virtq *vq, uint16_t size, struct virtq_desc *desc, struct virtq_avail *avail,
                struct virtq_used *used);

/*
 * Queue one request built from `out` device-readable buffers followed
 * by `in` device-writable buffers (sgs holds out+in entries, readable
 * first). Writes the descriptor chain and the next available-ring
 * slot, and advances avail->idx. Returns the chain head id, or -1
 * when the queue lacks the descriptors.
 *
 * The caller must issue its write barrier *before* calling (buffer
 * contents) and notify the device after; the avail->idx store here is
 * the publish.
 */
int virtq_add(struct virtq *vq, const struct virtq_sg *sgs, uint16_t out, uint16_t in);

/*
 * Consume one completion from the used ring, if any: recycles the
 * chain's descriptors onto the free list and returns the chain head
 * id (*len_out = device-written byte count), -1 when the used ring
 * has nothing new, or -2 when the device wrote an out-of-range id
 * (the caller must treat the queue as broken). The caller is
 * responsible for the read barrier between seeing used->idx advance
 * and reading the element.
 */
int virtq_take_used(struct virtq *vq, uint32_t *len_out);
