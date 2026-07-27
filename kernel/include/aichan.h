/*
 * aichan.h - the AI host channel: a real 16550 UART byte link (COM2).
 *
 * This is the guest side of the Phase 6 AI bridge: a plain serial line
 * to a helper process on the host that relays to the Claude API. It is
 * deliberately a *real* device, not a paravirtual one — the same
 * 16550 the console driver uses, on the second port (COM2 / 0x2F8) —
 * so it works on real hardware (the target board's serial header, with
 * a USB-serial adapter on the host), not only under QEMU. QEMU merely
 * emulates the same chip, so the driver is identical in both worlds.
 *
 * The transport is intentionally a thin byte pipe: the message framing,
 * `/dev/ai`, and the `aid` daemon layer on top of it in later slices,
 * and when real networking arrives (Phase 9) the API above this stays
 * put while the bytes move from a UART to a TCP socket.
 *
 * v1 is polled (no IRQ), like virtio-blk: read/write spin on the line
 * status register with a bounded timeout. Absent the UART (loopback
 * self-test fails), every entry point degrades quietly.
 *
 * Port note: COM2 is used so the channel never collides with the COM1
 * debug console. On a board whose only serial header is COM1, build
 * with the console on VGA and point AICHAN_PORT at COM1 instead — see
 * docs/book/appendix-m-real-hardware.md.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

/* Probe and initialize the channel's UART (loopback self-test).
 * Prints a boot marker either way. Requires the timer (for the I/O
 * timeouts). */
void aichan_init(void);

/* Nonzero once the UART is present and initialized. */
int aichan_present(void);

/* Send every byte of [buf,len) to the host, polling the transmit
 * holding register between bytes. Returns len, or -ENODEV if the
 * channel is absent, or -EIO if the transmitter wedges. */
long aichan_write(const void *buf, size_t len);

/* Read up to `len` bytes from the host, waiting up to `timeout_ms`
 * for the first byte, then draining whatever else is immediately
 * available. Returns the count (0 on timeout with nothing received),
 * or -ENODEV if the channel is absent. */
long aichan_read(void *buf, size_t len, uint64_t timeout_ms);

/* Boot-time round-trip check (parallels block_selftest): greets the
 * host, then reads one line and echoes it. A no-op when the UART is
 * absent, and bounded by a timeout so a boot with no host peer never
 * hangs. */
void aichan_selftest(void);
