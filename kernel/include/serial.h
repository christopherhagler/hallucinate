/*
 * serial.h - 16550 UART console transport (COM1).
 */
#ifndef HL_SERIAL_H
#define HL_SERIAL_H

#include <stdbool.h>

/*
 * Initialize COM1 at 115200 8N1 with FIFOs enabled. Verified with a loopback
 * self-test; returns false (and disables serial output) if the UART is absent
 * or broken. Safe to call exactly once, before any serial_putc.
 */
bool serial_init(void);

/* Write one byte. No-op if init failed. Callers handle LF -> CRLF policy. */
void serial_putc(char c);

#endif /* HL_SERIAL_H */
