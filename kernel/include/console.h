/*
 * console.h - kernel console: fans characters out to every active sink
 * (serial COM1 and the VGA text screen).
 */
#ifndef HL_CONSOLE_H
#define HL_CONSOLE_H

#include <stddef.h>

/* Initialize all sinks. Returns after the console is usable for output. */
void console_init(void);

void console_putc(char c);
void console_write(const char *buf, size_t len);

#endif /* HL_CONSOLE_H */
