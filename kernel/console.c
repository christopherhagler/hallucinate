/*
 * console.c - kernel console fan-out.
 *
 * Serial receives LF as CRLF so the transcript is readable in any terminal;
 * the VGA driver treats LF as a full newline on its own.
 */
#include <console.h>

#include <serial.h>
#include <vga.h>

void console_init(void) {
    /* Serial may legitimately be absent; VGA text mode is always there on
     * the BIOS boot path. serial_init self-disables on failure. */
    (void)serial_init();
    vga_init();
}

void console_putc(char c) {
    if (c == '\n') {
        serial_putc('\r');
    }
    serial_putc(c);
    vga_putc(c);
}

void console_write(const char *buf, size_t len) {
    for (size_t i = 0; i < len; i++) {
        console_putc(buf[i]);
    }
}
