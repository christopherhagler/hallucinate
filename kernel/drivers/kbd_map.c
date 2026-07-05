/*
 * kbd_map.c - PS/2 scancode set 1 translation state machine.
 */
#include <kbd_map.h>

#define SC_RELEASE   0x80
#define SC_E0_PREFIX 0xE0

#define SC_LSHIFT 0x2A
#define SC_RSHIFT 0x36
#define SC_CTRL   0x1D
#define SC_ALT    0x38
#define SC_CAPS   0x3A

#define SC_KP_ENTER 0x1C /* with E0 prefix */
#define SC_KP_SLASH 0x35 /* with E0 prefix */

/* Make-code to ASCII, scancode set 1, US layout, codes 0x00-0x39. */
static const char map_plain[0x3A] = {
    0,    0x1B, '1', '2', '3', '4', '5', '6', '7', '8', '9',  '0', '-', '=',  '\b',
    '\t', 'q',  'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p',  '[', ']', '\n', 0,
    'a',  's',  'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0,   '\\', 'z',
    'x',  'c',  'v', 'b', 'n', 'm', ',', '.', '/', 0,   '*',  0,   ' ',
};

static const char map_shift[0x3A] = {
    0,    0x1B, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+',  '\b',
    '\t', 'Q',  'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n', 0,
    'A',  'S',  'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', 0,   '|',  'Z',
    'X',  'C',  'V', 'B', 'N', 'M', '<', '>', '?', 0,   '*', 0,   ' ',
};

static bool shift_active(const struct kbd_state *st) {
    return st->shift_l || st->shift_r;
}

int kbd_map_feed(struct kbd_state *st, uint8_t sc) {
    if (sc == SC_E0_PREFIX) {
        st->e0 = true;
        return 0;
    }

    bool e0 = st->e0;
    st->e0 = false;
    bool release = (sc & SC_RELEASE) != 0;
    uint8_t code = sc & (uint8_t)~SC_RELEASE;

    /* Modifiers: track make and break. The E0 variants (right ctrl,
     * right alt) share the plain codes and are folded together. */
    switch (code) {
    case SC_LSHIFT:
        if (!e0) { /* E0 2A is a fake-shift artifact; ignore it */
            st->shift_l = !release;
        }
        return 0;
    case SC_RSHIFT:
        if (!e0) {
            st->shift_r = !release;
        }
        return 0;
    case SC_CTRL:
        st->ctrl = !release;
        return 0;
    case SC_ALT:
        st->alt = !release;
        return 0;
    case SC_CAPS:
        if (!release) {
            st->caps = !st->caps;
        }
        return 0;
    default:
        break;
    }

    if (release) {
        return 0;
    }

    if (e0) {
        /* Extended keys: only the two that map to ASCII produce
         * output; the rest (arrows, home, ...) are consumed. */
        if (code == SC_KP_ENTER) {
            return '\n';
        }
        if (code == SC_KP_SLASH) {
            return '/';
        }
        return 0;
    }

    if (code >= sizeof(map_plain)) {
        return 0;
    }

    char c = (char)(shift_active(st) ? map_shift[code] : map_plain[code]);
    if (c == 0) {
        return 0;
    }

    /* Ctrl+letter yields the control character (Ctrl+C = 3, ...). */
    if (st->ctrl) {
        if (c >= 'a' && c <= 'z') {
            return c - 'a' + 1;
        }
        if (c >= 'A' && c <= 'Z') {
            return c - 'A' + 1;
        }
        return 0;
    }

    /* Caps lock inverts letter case; shift+caps restores lowercase. */
    if (st->caps) {
        if (c >= 'a' && c <= 'z') {
            c = (char)(c - 'a' + 'A');
        } else if (c >= 'A' && c <= 'Z') {
            c = (char)(c - 'A' + 'a');
        }
    }
    return c;
}
