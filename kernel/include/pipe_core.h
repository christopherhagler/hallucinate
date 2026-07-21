/*
 * pipe_core.h - pipe ring buffer, pure bookkeeping.
 *
 * Plain C data and index arithmetic only (no allocation, no
 * scheduling) so the wraparound math is unit-tested on the host
 * (tests/host/test_pipe_core.c). The kernel side (kernel/fs/pipe.c)
 * supplies the backing byte array, blocks when the buffer can't
 * satisfy a call, and wakes waiters when it changes the fill level.
 *
 * A pipe is a byte stream, not a message queue: pipe_core_read and
 * pipe_core_write copy as many bytes as the buffer currently permits
 * (up to `len`) and report how many, exactly like a single read(2)/
 * write(2) on a real pipe would transfer in one call — short
 * transfers are expected, not an error, and it is the caller's job to
 * loop (for a full write) or accept a short result (for a read).
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

struct pipe_core {
    uint8_t *buf; /* caller-owned, `cap` bytes */
    size_t cap;
    size_t head;  /* next byte to read */
    size_t count; /* bytes currently buffered */
};

/* Bind `buf` (cap bytes, caller-owned and caller-freed) as the ring. */
void pipe_core_init(struct pipe_core *pc, uint8_t *buf, size_t cap);

/* Bytes immediately available to a read, and free space for a write. */
size_t pipe_core_avail(const struct pipe_core *pc);
size_t pipe_core_space(const struct pipe_core *pc);

/* Copy up to `len` buffered bytes into `dst`; returns the count moved
 * (0 if the buffer is empty). Never blocks, never fails. */
size_t pipe_core_read(struct pipe_core *pc, uint8_t *dst, size_t len);

/* Copy up to `len` bytes from `src` into the buffer, limited to the
 * free space available right now; returns the count moved (0 if the
 * buffer is full). Never blocks, never fails. */
size_t pipe_core_write(struct pipe_core *pc, const uint8_t *src, size_t len);
