/*
 * test_kbd.c - host unit tests for the PS/2 scancode translation
 * state machine (kernel/drivers/kbd_map.c).
 */
#include "test.h"

#include <kbd_map.h>

/* Scancodes used by the tests (set 1, US layout). */
#define SC(make)    (make)
#define BREAK(make) ((make) | 0x80)

#define SC_A           0x1E
#define SC_C           0x2E
#define SC_ONE         0x02
#define SC_SEMI        0x27
#define SC_ENTER       0x1C
#define SC_SPACE       0x39
#define SC_BSPACE      0x0E
#define SC_LSHIFT      0x2A
#define SC_RSHIFT      0x36
#define SC_CTRL        0x1D
#define SC_CAPS        0x3A
#define SC_E0          0xE0
#define SC_KP_SLASH    0x35
#define SC_ARROW_RIGHT 0x4D

TEST(kbd_plain_keys) {
    struct kbd_state st = {0};
    ASSERT_EQ_INT('a', kbd_map_feed(&st, SC(SC_A)));
    ASSERT_EQ_INT(0, kbd_map_feed(&st, BREAK(SC_A)));
    ASSERT_EQ_INT('1', kbd_map_feed(&st, SC(SC_ONE)));
    ASSERT_EQ_INT(';', kbd_map_feed(&st, SC(SC_SEMI)));
    ASSERT_EQ_INT('\n', kbd_map_feed(&st, SC(SC_ENTER)));
    ASSERT_EQ_INT(' ', kbd_map_feed(&st, SC(SC_SPACE)));
    ASSERT_EQ_INT('\b', kbd_map_feed(&st, SC(SC_BSPACE)));
}

TEST(kbd_shift) {
    struct kbd_state st = {0};
    ASSERT_EQ_INT(0, kbd_map_feed(&st, SC(SC_LSHIFT)));
    ASSERT_EQ_INT('A', kbd_map_feed(&st, SC(SC_A)));
    ASSERT_EQ_INT('!', kbd_map_feed(&st, SC(SC_ONE)));
    ASSERT_EQ_INT(':', kbd_map_feed(&st, SC(SC_SEMI)));
    ASSERT_EQ_INT(0, kbd_map_feed(&st, BREAK(SC_LSHIFT)));
    ASSERT_EQ_INT('a', kbd_map_feed(&st, SC(SC_A)));

    /* Right shift behaves identically. */
    ASSERT_EQ_INT(0, kbd_map_feed(&st, SC(SC_RSHIFT)));
    ASSERT_EQ_INT('A', kbd_map_feed(&st, SC(SC_A)));
    ASSERT_EQ_INT(0, kbd_map_feed(&st, BREAK(SC_RSHIFT)));
}

TEST(kbd_caps_lock) {
    struct kbd_state st = {0};
    ASSERT_EQ_INT(0, kbd_map_feed(&st, SC(SC_CAPS)));
    ASSERT_EQ_INT(0, kbd_map_feed(&st, BREAK(SC_CAPS)));
    ASSERT_EQ_INT('A', kbd_map_feed(&st, SC(SC_A)));
    /* Caps affects letters only. */
    ASSERT_EQ_INT('1', kbd_map_feed(&st, SC(SC_ONE)));
    /* Shift + caps -> lowercase. */
    ASSERT_EQ_INT(0, kbd_map_feed(&st, SC(SC_LSHIFT)));
    ASSERT_EQ_INT('a', kbd_map_feed(&st, SC(SC_A)));
    ASSERT_EQ_INT(0, kbd_map_feed(&st, BREAK(SC_LSHIFT)));
    /* Toggle off. */
    ASSERT_EQ_INT(0, kbd_map_feed(&st, SC(SC_CAPS)));
    ASSERT_EQ_INT('a', kbd_map_feed(&st, SC(SC_A)));
}

TEST(kbd_ctrl) {
    struct kbd_state st = {0};
    ASSERT_EQ_INT(0, kbd_map_feed(&st, SC(SC_CTRL)));
    ASSERT_EQ_INT(3, kbd_map_feed(&st, SC(SC_C))); /* Ctrl+C */
    ASSERT_EQ_INT(1, kbd_map_feed(&st, SC(SC_A))); /* Ctrl+A */
    /* Ctrl+1 has no control mapping. */
    ASSERT_EQ_INT(0, kbd_map_feed(&st, SC(SC_ONE)));
    ASSERT_EQ_INT(0, kbd_map_feed(&st, BREAK(SC_CTRL)));
    ASSERT_EQ_INT('c', kbd_map_feed(&st, SC(SC_C)));
}

TEST(kbd_extended_keys) {
    struct kbd_state st = {0};
    /* Arrow keys are consumed silently. */
    ASSERT_EQ_INT(0, kbd_map_feed(&st, SC_E0));
    ASSERT_EQ_INT(0, kbd_map_feed(&st, SC(SC_ARROW_RIGHT)));
    ASSERT_EQ_INT(0, kbd_map_feed(&st, SC_E0));
    ASSERT_EQ_INT(0, kbd_map_feed(&st, BREAK(SC_ARROW_RIGHT)));
    /* Keypad enter and slash produce characters. */
    ASSERT_EQ_INT(0, kbd_map_feed(&st, SC_E0));
    ASSERT_EQ_INT('\n', kbd_map_feed(&st, SC(SC_ENTER)));
    ASSERT_EQ_INT(0, kbd_map_feed(&st, SC_E0));
    ASSERT_EQ_INT('/', kbd_map_feed(&st, SC(SC_KP_SLASH)));
    /* E0 2A fake shift must not stick shift on. */
    ASSERT_EQ_INT(0, kbd_map_feed(&st, SC_E0));
    ASSERT_EQ_INT(0, kbd_map_feed(&st, SC(SC_LSHIFT)));
    ASSERT_EQ_INT('a', kbd_map_feed(&st, SC(SC_A)));
    /* The E0 prefix applies to exactly one following byte. */
    ASSERT_EQ_INT('/', kbd_map_feed(&st, SC(SC_KP_SLASH)));
}
