/*
 * vga.h - VGA text-mode console (80x25, BIOS mode 3).
 */
#ifndef HL_VGA_H
#define HL_VGA_H

/* Clear the screen and home the hardware cursor. Call once before vga_putc. */
void vga_init(void);

/* Write one character; handles \n, \r, \t and scrolling. */
void vga_putc(char c);

#endif /* HL_VGA_H */
