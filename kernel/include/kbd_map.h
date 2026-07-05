/*
 * kbd_map.h - PS/2 scancode set 1 to ASCII translation (US layout).
 *
 * Pure logic, no I/O: the same code is unit-tested on the host
 * (tests/host/test_kbd.c) and driven by the keyboard IRQ handler in
 * the kernel.
 *
 * Scope: scancode set 1 (as delivered by the 8042 with translation
 * enabled), US QWERTY, shift/ctrl/caps-lock state, the 0xE0 extended
 * prefix (keypad enter and keypad slash produce characters; other
 * extended keys are consumed silently until there is a consumer for
 * key events rather than characters).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

struct kbd_state {
    bool shift_l;
    bool shift_r;
    bool ctrl;
    bool alt;
    bool caps;
    bool e0; /* previous byte was the 0xE0 extended prefix */
};

/*
 * Feed one scancode byte through the state machine.
 * Returns the ASCII character produced (> 0), or 0 if the byte
 * produced none (modifier, release, extended, or unmapped key).
 */
int kbd_map_feed(struct kbd_state *st, uint8_t scancode);
