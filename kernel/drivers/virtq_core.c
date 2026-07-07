/*
 * virtq_core.c - split virtqueue descriptor accounting (pure).
 *
 * See virtq_core.h for the contract. This file must stay free of
 * kernel dependencies: it also compiles on macOS for the host test
 * suite, where every path runs under ASan/UBSan.
 */
#include <virtq_core.h>

size_t virtq_desc_bytes(uint16_t size) {
    return (size_t)size * sizeof(struct virtq_desc);
}

size_t virtq_avail_bytes(uint16_t size) {
    return sizeof(struct virtq_avail) + ((size_t)size * sizeof(uint16_t));
}

size_t virtq_used_bytes(uint16_t size) {
    return sizeof(struct virtq_used) + ((size_t)size * sizeof(struct virtq_used_elem));
}

void virtq_init(struct virtq *vq, uint16_t size, struct virtq_desc *desc, struct virtq_avail *avail,
                struct virtq_used *used) {
    vq->size = size;
    vq->desc = desc;
    vq->avail = avail;
    vq->used = used;
    vq->free_head = 0;
    vq->num_free = size;
    vq->last_used = 0;
    for (uint16_t i = 0; i < size; i++) {
        desc[i].addr = 0;
        desc[i].len = 0;
        desc[i].flags = (uint16_t)((i + 1 < size) ? VIRTQ_DESC_F_NEXT : 0);
        desc[i].next = (uint16_t)(i + 1 < size ? i + 1 : 0);
    }
}

int virtq_add(struct virtq *vq, const struct virtq_sg *sgs, uint16_t out, uint16_t in) {
    uint16_t total = (uint16_t)(out + in);
    if (total == 0 || total > vq->num_free) {
        return -1;
    }

    uint16_t head = vq->free_head;
    uint16_t idx = head;
    uint16_t prev = head;
    for (uint16_t i = 0; i < total; i++) {
        struct virtq_desc *d = &vq->desc[idx];
        d->addr = sgs[i].addr;
        d->len = sgs[i].len;
        d->flags = (uint16_t)((i >= out) ? VIRTQ_DESC_F_WRITE : 0);
        if (i + 1 < total) {
            d->flags |= VIRTQ_DESC_F_NEXT;
        }
        prev = idx;
        idx = d->next;
    }
    vq->desc[prev].flags &= (uint16_t)~VIRTQ_DESC_F_NEXT;
    vq->free_head = idx;
    vq->num_free = (uint16_t)(vq->num_free - total);

    vq->avail->ring[vq->avail->idx & (uint16_t)(vq->size - 1)] = head;
    vq->avail->idx++;
    return head;
}

int virtq_take_used(struct virtq *vq, uint32_t *len_out) {
    if (vq->last_used == vq->used->idx) {
        return -1;
    }
    const struct virtq_used_elem *e = &vq->used->ring[vq->last_used & (uint16_t)(vq->size - 1)];
    vq->last_used++;
    uint16_t id = (uint16_t)e->id;
    if (id >= vq->size) {
        return -2; /* device wrote garbage; caller treats as fatal */
    }
    if (len_out != NULL) {
        *len_out = e->len;
    }

    /* Recycle the chain: walk to its tail, then splice onto the free
     * list. The chain length is implicit in the NEXT flags the add
     * path wrote; bound the walk so corrupted flags cannot loop us. */
    uint16_t tail = id;
    uint16_t count = 1;
    while ((vq->desc[tail].flags & VIRTQ_DESC_F_NEXT) && count < vq->size) {
        tail = vq->desc[tail].next;
        count++;
    }
    vq->desc[tail].flags = VIRTQ_DESC_F_NEXT;
    vq->desc[tail].next = vq->free_head;
    vq->free_head = id;
    vq->num_free = (uint16_t)(vq->num_free + count);
    return id;
}
