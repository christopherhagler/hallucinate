/*
 * aichan.c - the AI host channel over a real 16550 UART (COM2).
 *
 * See aichan.h for the rationale: a *real* device (the same 16550 the
 * console uses, on the second port) so the AI bridge works on the target
 * board's serial header, not only under QEMU. This mirrors serial.c's
 * COM1 driver but adds the receive path the console never needs, and is
 * the thin byte transport that /dev/ai and the `aid` daemon sit on in
 * later slices.
 *
 * Polled, no IRQ (like virtio-blk v1): every wait is bounded so a boot
 * with a wedged line or no host peer degrades quietly instead of hanging.
 */
#include <aichan.h>

#include <errno.h>
#include <stdbool.h>

#include <arch/x86_64/io.h>
#include <kprintf.h>
#include <timer.h>

/*
 * COM2 by default so the channel never collides with the COM1 debug
 * console. On a board whose only serial header is COM1, build the console
 * on VGA and point this at 0x3F8 instead (appendix-m-real-hardware.md).
 */
#ifndef AICHAN_PORT
#define AICHAN_PORT 0x2F8
#endif

/* Register offsets (DLAB=0 unless noted) — the same 16550 map as serial.c. */
#define REG_DATA 0 /* THR/RBR; divisor low with DLAB=1 */
#define REG_IER  1 /* interrupt enable; divisor high with DLAB=1 */
#define REG_FCR  2 /* FIFO control */
#define REG_LCR  3 /* line control */
#define REG_MCR  4 /* modem control */
#define REG_LSR  5 /* line status */

#define LCR_8N1    0x03
#define LCR_DLAB   0x80
#define FCR_ENABLE 0xC7 /* enable, clear both FIFOs, 14-byte threshold */
#define MCR_LOOP   0x10
#define MCR_NORMAL 0x0F /* DTR | RTS | OUT1 | OUT2 */
#define LSR_DR     0x01 /* data ready: a byte waits in the receive register */
#define LSR_THRE   0x20 /* transmit holding register empty */

/* Per-byte transmit-drain budget, matching serial.c's spin count. */
#define TX_SPIN 100000u

static bool aichan_ok = false;

void aichan_init(void) {
    outb(AICHAN_PORT + REG_IER, 0x00); /* no interrupts (polled v1) */
    outb(AICHAN_PORT + REG_LCR, LCR_DLAB);
    outb(AICHAN_PORT + REG_DATA, 0x01); /* divisor 1 = 115200 baud */
    outb(AICHAN_PORT + REG_IER, 0x00);
    outb(AICHAN_PORT + REG_LCR, LCR_8N1);
    outb(AICHAN_PORT + REG_FCR, FCR_ENABLE);

    /* Loopback self-test: a byte sent must come straight back. This is
     * internal to the UART, so it proves the chip is present even when
     * nothing is wired to the port. */
    outb(AICHAN_PORT + REG_MCR, MCR_LOOP | 0x0A);
    outb(AICHAN_PORT + REG_DATA, 0xAE);
    if (inb(AICHAN_PORT + REG_DATA) != 0xAE) {
        aichan_ok = false;
        kprintf("aichan: no UART at COM2 (port %#x)\n", AICHAN_PORT);
        return;
    }

    outb(AICHAN_PORT + REG_MCR, MCR_NORMAL);
    aichan_ok = true;
    kprintf("aichan: COM2 UART ready (port %#x, 115200 8N1, polled)\n", AICHAN_PORT);
}

int aichan_present(void) {
    return aichan_ok ? 1 : 0;
}

long aichan_write(const void *buf, size_t len) {
    if (!aichan_ok) {
        return -ENODEV;
    }
    const uint8_t *p = buf;
    for (size_t i = 0; i < len; i++) {
        bool sent = false;
        for (uint32_t spin = 0; spin < TX_SPIN; spin++) {
            if (inb(AICHAN_PORT + REG_LSR) & LSR_THRE) {
                outb(AICHAN_PORT + REG_DATA, p[i]);
                sent = true;
                break;
            }
        }
        if (!sent) {
            aichan_ok = false; /* transmitter wedged; stop trusting the line */
            return -EIO;
        }
    }
    return (long)len;
}

long aichan_read(void *buf, size_t len, uint64_t timeout_ms) {
    if (!aichan_ok) {
        return -ENODEV;
    }
    if (len == 0) {
        return 0;
    }
    uint8_t *p = buf;

    /* Wait up to timeout_ms for the first byte to arrive. */
    uint64_t deadline = timer_uptime_ms() + timeout_ms;
    while (!(inb(AICHAN_PORT + REG_LSR) & LSR_DR)) {
        if (timer_uptime_ms() >= deadline) {
            return 0; /* nothing came */
        }
    }

    /* Drain whatever else is immediately available, up to len. */
    size_t n = 0;
    while (n < len && (inb(AICHAN_PORT + REG_LSR) & LSR_DR)) {
        p[n++] = inb(AICHAN_PORT + REG_DATA);
    }
    return (long)n;
}

void aichan_selftest(void) {
    if (!aichan_ok) {
        kprintf("aichan: selftest skipped (no UART)\n");
        return;
    }

    /* Greet the host. If a peer is listening it sees this; if not, the
     * bytes vanish into an unconnected line — harmless. */
    static const char greeting[] = "aichan: hello from guest\n";
    aichan_write(greeting, sizeof(greeting) - 1);

    /*
     * Read one line from the host and echo it back verbatim — a real
     * round trip that exercises both directions. aichan_read only drains
     * what is immediately available, so accumulate across reads until a
     * newline arrives or an overall budget expires. With no host peer
     * (real hardware, no bridge running) this simply times out and says
     * so; boot never hangs.
     */
    char line[128];
    size_t n = 0;
    bool got_line = false;
    uint64_t deadline = timer_uptime_ms() + 2000;
    while (n < sizeof(line) && timer_uptime_ms() < deadline) {
        long r = aichan_read(line + n, sizeof(line) - n, 250);
        if (r < 0) {
            break;
        }
        n += (size_t)r;
        if (n > 0 && line[n - 1] == '\n') {
            got_line = true;
            break;
        }
    }

    if (!got_line || n == 0) {
        kprintf("aichan: selftest: no host peer (round-trip skipped)\n");
        return;
    }

    if (aichan_write(line, n) != (long)n) {
        kprintf("aichan: selftest: echo write failed\n");
        return;
    }
    kprintf("aichan: selftest ok (echoed %u bytes)\n", (unsigned)n);
}
