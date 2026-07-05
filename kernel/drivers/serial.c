/*
 * serial.c - 16550 UART driver for the COM1 console.
 *
 * Polled transmit only; interrupt-driven I/O arrives with the IDT in Phase 2.
 */
#include <serial.h>

#include <stdbool.h>
#include <stdint.h>

#include <arch/x86_64/io.h>

#define COM1 0x3F8

/* Register offsets (DLAB=0 unless noted). */
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
#define LSR_THRE   0x20 /* transmit holding register empty */

static bool serial_ok = false;

bool serial_init(void) {
    outb(COM1 + REG_IER, 0x00); /* no interrupts */
    outb(COM1 + REG_LCR, LCR_DLAB);
    outb(COM1 + REG_DATA, 0x01); /* divisor 1 = 115200 baud */
    outb(COM1 + REG_IER, 0x00);
    outb(COM1 + REG_LCR, LCR_8N1);
    outb(COM1 + REG_FCR, FCR_ENABLE);

    /* Loopback self-test: a byte sent must come straight back. */
    outb(COM1 + REG_MCR, MCR_LOOP | 0x0A);
    outb(COM1 + REG_DATA, 0xAE);
    if (inb(COM1 + REG_DATA) != 0xAE) {
        serial_ok = false;
        return false;
    }

    outb(COM1 + REG_MCR, MCR_NORMAL);
    serial_ok = true;
    return true;
}

void serial_putc(char c) {
    if (!serial_ok) {
        return;
    }
    /* Bounded wait so a wedged UART degrades output instead of hanging boot. */
    for (uint32_t spin = 0; spin < 100000; spin++) {
        if (inb(COM1 + REG_LSR) & LSR_THRE) {
            outb(COM1 + REG_DATA, (uint8_t)c);
            return;
        }
    }
    serial_ok = false; /* transmitter never drained; stop trying */
}
