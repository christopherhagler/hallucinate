/*
 * pipe_core.c - pipe ring buffer, pure bookkeeping. See pipe_core.h.
 */
#include <pipe_core.h>

#include <string.h>

void pipe_core_init(struct pipe_core *pc, uint8_t *buf, size_t cap) {
    pc->buf = buf;
    pc->cap = cap;
    pc->head = 0;
    pc->count = 0;
}

size_t pipe_core_avail(const struct pipe_core *pc) {
    return pc->count;
}

size_t pipe_core_space(const struct pipe_core *pc) {
    return pc->cap - pc->count;
}

size_t pipe_core_read(struct pipe_core *pc, uint8_t *dst, size_t len) {
    size_t n = (len < pc->count) ? len : pc->count;
    size_t first = pc->cap - pc->head;
    if (first > n) {
        first = n;
    }
    memcpy(dst, pc->buf + pc->head, first);
    memcpy(dst + first, pc->buf, n - first);
    pc->head = (pc->head + n) % pc->cap;
    pc->count -= n;
    return n;
}

size_t pipe_core_write(struct pipe_core *pc, const uint8_t *src, size_t len) {
    size_t space = pc->cap - pc->count;
    size_t n = (len < space) ? len : space;
    size_t tail = (pc->head + pc->count) % pc->cap;
    size_t first = pc->cap - tail;
    if (first > n) {
        first = n;
    }
    memcpy(pc->buf + tail, src, first);
    memcpy(pc->buf, src + first, n - first);
    pc->count += n;
    return n;
}
