/*
 * test_virtq.c - unit tests for the split virtqueue core.
 *
 * The pure ring bookkeeping (kernel/drivers/virtq_core.c) runs here
 * under ASan/UBSan playing both sides: the tests act as the device,
 * consuming the available ring and producing used entries the way a
 * VIRTIO 1.2 device would.
 */
#include <stdint.h>
#include <stdlib.h>

#include <virtq_core.h>

#include "test.h"

#define QSIZE 8

struct ring_mem {
    struct virtq vq;
    struct virtq_desc *desc;
    struct virtq_avail *avail;
    struct virtq_used *used;
    uint16_t device_avail; /* device-side shadow of avail consumption */
};

static struct ring_mem *ring_new(uint16_t size) {
    struct ring_mem *r = calloc(1, sizeof(*r));
    r->desc = calloc(1, virtq_desc_bytes(size));
    r->avail = calloc(1, virtq_avail_bytes(size));
    r->used = calloc(1, virtq_used_bytes(size));
    virtq_init(&r->vq, size, r->desc, r->avail, r->used);
    return r;
}

static void ring_free(struct ring_mem *r) {
    free(r->desc);
    free(r->avail);
    free(r->used);
    free(r);
}

/* Device side: take the next available chain head, or -1. */
static int device_pop(struct ring_mem *r) {
    if (r->device_avail == r->avail->idx) {
        return -1;
    }
    uint16_t head = r->avail->ring[r->device_avail & (uint16_t)(r->vq.size - 1)];
    r->device_avail++;
    return head;
}

/* Device side: mark a chain completed with `len` bytes written. */
static void device_complete(struct ring_mem *r, uint16_t head, uint32_t len) {
    struct virtq_used_elem *e = &r->used->ring[r->used->idx & (uint16_t)(r->vq.size - 1)];
    e->id = head;
    e->len = len;
    r->used->idx++;
}

TEST(virtq_layout_sizes) {
    /* VIRTIO 1.2 §2.7: 16 bytes per descriptor; the avail/used rings
     * are 4 + 2n and 4 + 8n without VIRTIO_F_EVENT_IDX (which we do
     * not negotiate). */
    ASSERT_EQ_INT(16 * 128, virtq_desc_bytes(128));
    ASSERT_EQ_INT(4 + (2 * 128), (int)virtq_avail_bytes(128));
    ASSERT_EQ_INT(4 + (8 * 128), (int)virtq_used_bytes(128));
}

TEST(virtq_add_builds_chain) {
    struct ring_mem *r = ring_new(QSIZE);
    struct virtq_sg sgs[3] = {{0x1000, 16}, {0x2000, 4096}, {0x3000, 1}};

    int head = virtq_add(&r->vq, sgs, 1, 2);
    ASSERT_TRUE(head >= 0);
    ASSERT_EQ_INT(QSIZE - 3, r->vq.num_free);
    ASSERT_EQ_INT(1, r->avail->idx);
    ASSERT_EQ_INT(head, r->avail->ring[0]);

    /* Walk the chain the way the device would. */
    const struct virtq_desc *d0 = &r->desc[head];
    ASSERT_EQ_INT(0x1000, (long long)d0->addr);
    ASSERT_EQ_INT(16, d0->len);
    ASSERT_TRUE(d0->flags & VIRTQ_DESC_F_NEXT);
    ASSERT_TRUE(!(d0->flags & VIRTQ_DESC_F_WRITE));

    const struct virtq_desc *d1 = &r->desc[d0->next];
    ASSERT_EQ_INT(0x2000, (long long)d1->addr);
    ASSERT_TRUE(d1->flags & VIRTQ_DESC_F_NEXT);
    ASSERT_TRUE(d1->flags & VIRTQ_DESC_F_WRITE);

    const struct virtq_desc *d2 = &r->desc[d1->next];
    ASSERT_EQ_INT(0x3000, (long long)d2->addr);
    ASSERT_TRUE(!(d2->flags & VIRTQ_DESC_F_NEXT)); /* chain ends */
    ASSERT_TRUE(d2->flags & VIRTQ_DESC_F_WRITE);
    ring_free(r);
}

TEST(virtq_complete_recycles_descriptors) {
    struct ring_mem *r = ring_new(QSIZE);
    struct virtq_sg sgs[3] = {{0x1000, 16}, {0x2000, 4096}, {0x3000, 1}};

    int head = virtq_add(&r->vq, sgs, 2, 1);
    ASSERT_EQ_INT(head, device_pop(r));
    device_complete(r, (uint16_t)head, 1);

    uint32_t len = 0;
    ASSERT_EQ_INT(head, virtq_take_used(&r->vq, &len));
    ASSERT_EQ_INT(1, len);
    ASSERT_EQ_INT(QSIZE, r->vq.num_free); /* all descriptors back */
    ASSERT_EQ_INT(-1, virtq_take_used(&r->vq, &len));
    ring_free(r);
}

TEST(virtq_exhaustion_and_reuse) {
    struct ring_mem *r = ring_new(QSIZE);
    struct virtq_sg sg = {0x1000, 512};

    int heads[QSIZE];
    for (int i = 0; i < QSIZE; i++) {
        heads[i] = virtq_add(&r->vq, &sg, 1, 0);
        ASSERT_TRUE(heads[i] >= 0);
    }
    ASSERT_EQ_INT(0, r->vq.num_free);
    ASSERT_EQ_INT(-1, virtq_add(&r->vq, &sg, 1, 0)); /* full */

    /* Complete them all (in order) and reuse the queue twice over to
     * exercise index wrap-around. */
    for (int round = 0; round < 3; round++) {
        for (int i = 0; i < QSIZE; i++) {
            int h = (round == 0) ? heads[i] : device_pop(r);
            if (round > 0) {
                ASSERT_TRUE(h >= 0);
            } else {
                ASSERT_EQ_INT(h, device_pop(r));
            }
            device_complete(r, (uint16_t)h, 0);
            ASSERT_EQ_INT(h, virtq_take_used(&r->vq, NULL));
        }
        if (round < 2) {
            for (int i = 0; i < QSIZE; i++) {
                ASSERT_TRUE(virtq_add(&r->vq, &sg, 1, 0) >= 0);
            }
        }
    }
    ASSERT_EQ_INT(QSIZE, r->vq.num_free);
    ring_free(r);
}

TEST(virtq_rejects_bogus_used_id) {
    struct ring_mem *r = ring_new(QSIZE);
    struct virtq_sg sg = {0x1000, 512};
    int head = virtq_add(&r->vq, &sg, 1, 0);
    ASSERT_TRUE(head >= 0);

    device_complete(r, QSIZE + 3, 0); /* out-of-range id */
    ASSERT_EQ_INT(-2, virtq_take_used(&r->vq, NULL));
    ring_free(r);
}

TEST(virtq_mixed_chains_interleaved) {
    struct ring_mem *r = ring_new(QSIZE);
    struct virtq_sg a[2] = {{0xA000, 16}, {0xA100, 1}};
    struct virtq_sg b[3] = {{0xB000, 16}, {0xB100, 4096}, {0xB200, 1}};

    int ha = virtq_add(&r->vq, a, 1, 1);
    int hb = virtq_add(&r->vq, b, 2, 1);
    ASSERT_TRUE(ha >= 0 && hb >= 0);
    ASSERT_EQ_INT(QSIZE - 5, r->vq.num_free);

    /* Complete out of order: b first. */
    ASSERT_EQ_INT(ha, device_pop(r));
    ASSERT_EQ_INT(hb, device_pop(r));
    device_complete(r, (uint16_t)hb, 1);
    ASSERT_EQ_INT(hb, virtq_take_used(&r->vq, NULL));
    ASSERT_EQ_INT(QSIZE - 2, r->vq.num_free);
    device_complete(r, (uint16_t)ha, 1);
    ASSERT_EQ_INT(ha, virtq_take_used(&r->vq, NULL));
    ASSERT_EQ_INT(QSIZE, r->vq.num_free);
    ring_free(r);
}
