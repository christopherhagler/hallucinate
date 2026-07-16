/*
 * keyboard.c - PS/2 keyboard driver (IRQ 1, port 0x60).
 *
 * The IRQ handler drains the 8042 output buffer, runs each scancode
 * through the kbd_map state machine, and pushes resulting characters
 * into a ring buffer that keyboard_getchar() pops with interrupts
 * briefly disabled. When the ring is full the oldest input is
 * dropped: for interactive typing, keeping the newest keystrokes is
 * the right failure mode.
 */
#include <keyboard.h>

#include <stddef.h>
#include <stdint.h>

#include <arch/x86_64/cpu.h>
#include <arch/x86_64/io.h>
#include <arch/x86_64/irq.h>
#include <kbd_map.h>

#define KBD_DATA   0x60
#define KBD_STATUS 0x64
#define STATUS_OBF 0x01 /* output buffer full */

#define IRQ_KEYBOARD 1

#define RING_SIZE 256 /* power of two */

static struct kbd_state state;
static char ring[RING_SIZE];
static uint32_t ring_head; /* next write */
static uint32_t ring_tail; /* next read */

static void (*notify)(void); /* new-input callback (IRQ context) */

static void ring_push(char c) {
    uint32_t next = (ring_head + 1) % RING_SIZE;
    if (next == ring_tail) {
        ring_tail = (ring_tail + 1) % RING_SIZE; /* drop oldest */
    }
    ring[ring_head] = c;
    ring_head = next;
}

static void keyboard_irq(void) {
    int pushed = 0;
    while (inb(KBD_STATUS) & STATUS_OBF) {
        uint8_t sc = inb(KBD_DATA);
        int c = kbd_map_feed(&state, sc);
        if (c > 0) {
            ring_push((char)c);
            pushed = 1;
        }
    }
    if (pushed && notify != NULL) {
        notify();
    }
}

void keyboard_init(void) {
    /* Drain anything pending so the first IRQ starts clean. */
    while (inb(KBD_STATUS) & STATUS_OBF) {
        (void)inb(KBD_DATA);
    }
    irq_register(IRQ_KEYBOARD, keyboard_irq);
}

void keyboard_set_notify(void (*fn)(void)) {
    notify = fn;
}

int keyboard_getchar(void) {
    int c = -1;
    int enabled = cpu_interrupts_enabled();
    if (enabled) {
        cpu_disable_interrupts();
    }
    if (ring_tail != ring_head) {
        c = (uint8_t)ring[ring_tail];
        ring_tail = (ring_tail + 1) % RING_SIZE;
    }
    if (enabled) {
        cpu_enable_interrupts();
    }
    return c;
}
