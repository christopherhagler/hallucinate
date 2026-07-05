/*
 * vga.c - VGA text-mode console driver.
 *
 * The BIOS leaves the machine in text mode 3 (80x25, 16 colors) and the boot
 * page tables map the VGA aperture at KERNEL_VMA + 0xB8000. This driver will
 * be superseded by the framebuffer terminal in Phase 8 but keeps the same
 * console-sink interface.
 */
#include <vga.h>

#include <stdint.h>

#include <arch/x86_64/io.h>
#include <memlayout.h>
#include <string.h>

#define VGA_COLS  80
#define VGA_ROWS  25
#define VGA_ATTR  0x07 /* light grey on black */
#define TAB_WIDTH 8

#define CRTC_INDEX     0x3D4
#define CRTC_DATA      0x3D5
#define CRTC_CURSOR_HI 0x0E
#define CRTC_CURSOR_LO 0x0F

static volatile uint16_t *const vga_mem = (volatile uint16_t *)(KERNEL_VMA + 0xB8000);

static int cur_row;
static int cur_col;

static uint16_t cell(char c) {
    return (uint16_t)((uint16_t)VGA_ATTR << 8 | (uint8_t)c);
}

static void move_hw_cursor(void) {
    uint16_t pos = (uint16_t)((cur_row * VGA_COLS) + cur_col);
    outb(CRTC_INDEX, CRTC_CURSOR_HI);
    outb(CRTC_DATA, (uint8_t)(pos >> 8));
    outb(CRTC_INDEX, CRTC_CURSOR_LO);
    outb(CRTC_DATA, (uint8_t)pos);
}

static void scroll(void) {
    /* volatile qualifier dropped for the bulk move; the buffer is ordinary
     * memory and no other writer exists before interrupts are enabled. */
    uint16_t *mem = (uint16_t *)vga_mem;
    memmove(mem, mem + VGA_COLS, (size_t)(VGA_ROWS - 1) * VGA_COLS * 2);
    for (int col = 0; col < VGA_COLS; col++) {
        mem[((VGA_ROWS - 1) * VGA_COLS) + col] = cell(' ');
    }
}

void vga_init(void) {
    for (int i = 0; i < VGA_ROWS * VGA_COLS; i++) {
        vga_mem[i] = cell(' ');
    }
    cur_row = 0;
    cur_col = 0;
    move_hw_cursor();
}

void vga_putc(char c) {
    switch (c) {
    case '\n':
        cur_col = 0;
        cur_row++;
        break;
    case '\r':
        cur_col = 0;
        break;
    case '\t':
        cur_col = ((cur_col / TAB_WIDTH) + 1) * TAB_WIDTH;
        break;
    default:
        if ((uint8_t)c < 0x20) {
            break; /* other control characters are ignored */
        }
        vga_mem[(cur_row * VGA_COLS) + cur_col] = cell(c);
        cur_col++;
        break;
    }

    if (cur_col >= VGA_COLS) {
        cur_col = 0;
        cur_row++;
    }
    if (cur_row >= VGA_ROWS) {
        scroll();
        cur_row = VGA_ROWS - 1;
    }
    move_hw_cursor();
}
