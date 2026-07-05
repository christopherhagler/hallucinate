/*
 * fmt.c - freestanding printf-style formatting engine.
 *
 * Single implementation used by the kernel (kprintf, panic) and exercised by
 * the host unit test suite under ASan/UBSan. See fmt.h for the supported
 * conversion grammar.
 */
#include <fmt.h>

#include <stdbool.h>
#include <stdint.h>

#include <string.h>

#define FLAG_LEFT  (1u << 0) /* '-' */
#define FLAG_PLUS  (1u << 1) /* '+' */
#define FLAG_SPACE (1u << 2) /* ' ' */
#define FLAG_ZERO  (1u << 3) /* '0' */
#define FLAG_ALT   (1u << 4) /* '#' */

enum length_mod {
    LEN_NONE,
    LEN_HH,
    LEN_H,
    LEN_L,
    LEN_LL,
    LEN_Z,
    LEN_T,
};

/* Output sink: writes into a bounded buffer while counting the full length. */
struct sink {
    char *buf;
    size_t cap; /* bytes usable for characters (cap = size - 1), 0 if size == 0 */
    size_t len; /* number of characters produced so far (may exceed cap) */
};

static void sink_putc(struct sink *s, char c) {
    if (s->len < s->cap) {
        s->buf[s->len] = c;
    }
    s->len++;
}

static void sink_pad(struct sink *s, char c, int n) {
    for (int i = 0; i < n; i++) {
        sink_putc(s, c);
    }
}

static void sink_write(struct sink *s, const char *p, size_t n) {
    for (size_t i = 0; i < n; i++) {
        sink_putc(s, p[i]);
    }
}

/* Longest rendering: 64-bit octal (22 digits); buffer leaves headroom. */
#define NUMBUF 24

static const char DIGITS_LOWER[] = "0123456789abcdef";
static const char DIGITS_UPPER[] = "0123456789ABCDEF";

struct spec {
    unsigned flags;
    int width;     /* -1 = none */
    int precision; /* -1 = none */
};

/*
 * Emit one formatted integer. `mag` is the magnitude, `sign` is '-', '+', ' '
 * or 0 (no sign character), `prefix` is "" / "0x" / "0X" / "0".
 */
static void emit_int(struct sink *s, const struct spec *sp, uint64_t mag, char sign,
                     const char *prefix, unsigned base, const char *digits) {
    char tmp[NUMBUF];
    int ndigits = 0;

    do {
        tmp[ndigits++] = digits[mag % base];
        mag /= base;
    } while (mag != 0 && ndigits < NUMBUF);

    /* C99: precision 0 with value 0 prints no digits. */
    if (sp->precision == 0 && ndigits == 1 && tmp[0] == '0') {
        ndigits = 0;
    }

    int zeros = 0;
    if (sp->precision > ndigits) {
        zeros = sp->precision - ndigits;
    }

    int prefix_len = (int)strlen(prefix);
    int body = ndigits + zeros + prefix_len + (sign != 0 ? 1 : 0);
    int pad = (sp->width > body) ? sp->width - body : 0;

    /* '0' pads between sign/prefix and digits; ignored with '-' or precision. */
    bool zero_pad =
        (sp->flags & FLAG_ZERO) != 0 && (sp->flags & FLAG_LEFT) == 0 && sp->precision < 0;

    if (!(sp->flags & FLAG_LEFT) && !zero_pad) {
        sink_pad(s, ' ', pad);
    }
    if (sign != 0) {
        sink_putc(s, sign);
    }
    sink_write(s, prefix, (size_t)prefix_len);
    if (zero_pad) {
        sink_pad(s, '0', pad);
    }
    sink_pad(s, '0', zeros);
    for (int i = ndigits - 1; i >= 0; i--) {
        sink_putc(s, tmp[i]);
    }
    if (sp->flags & FLAG_LEFT) {
        sink_pad(s, ' ', pad);
    }
}

static void emit_str(struct sink *s, const struct spec *sp, const char *str) {
    if (str == NULL) {
        str = "(null)";
    }
    size_t n = (sp->precision >= 0) ? strnlen(str, (size_t)sp->precision) : strlen(str);
    int pad = (sp->width > (int)n) ? sp->width - (int)n : 0;

    if (!(sp->flags & FLAG_LEFT)) {
        sink_pad(s, ' ', pad);
    }
    sink_write(s, str, n);
    if (sp->flags & FLAG_LEFT) {
        sink_pad(s, ' ', pad);
    }
}

static void emit_char(struct sink *s, const struct spec *sp, char c) {
    int pad = (sp->width > 1) ? sp->width - 1 : 0;
    if (!(sp->flags & FLAG_LEFT)) {
        sink_pad(s, ' ', pad);
    }
    sink_putc(s, c);
    if (sp->flags & FLAG_LEFT) {
        sink_pad(s, ' ', pad);
    }
}

static uint64_t arg_unsigned(va_list *ap, enum length_mod len) {
    switch (len) {
    case LEN_HH:
        return (uint8_t)va_arg(*ap, unsigned int);
    case LEN_H:
        return (uint16_t)va_arg(*ap, unsigned int);
    case LEN_L:
        return va_arg(*ap, unsigned long);
    case LEN_LL:
        return va_arg(*ap, unsigned long long);
    case LEN_Z:
        return va_arg(*ap, size_t);
    case LEN_T:
        return (uint64_t)va_arg(*ap, ptrdiff_t);
    case LEN_NONE:
    default:
        return va_arg(*ap, unsigned int);
    }
}

static int64_t arg_signed(va_list *ap, enum length_mod len) {
    switch (len) {
    case LEN_HH:
        return (int8_t)va_arg(*ap, int);
    case LEN_H:
        return (int16_t)va_arg(*ap, int);
    case LEN_L:
        return va_arg(*ap, long);
    case LEN_LL:
        return va_arg(*ap, long long);
    case LEN_Z:
        return (int64_t)va_arg(*ap, size_t);
    case LEN_T:
        return va_arg(*ap, ptrdiff_t);
    case LEN_NONE:
    default:
        return va_arg(*ap, int);
    }
}

int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap) {
    struct sink s = {
        .buf = buf,
        .cap = (size > 0) ? size - 1 : 0,
        .len = 0,
    };
    va_list args;
    va_copy(args, ap);

    for (const char *p = fmt; *p != '\0'; p++) {
        if (*p != '%') {
            sink_putc(&s, *p);
            continue;
        }
        p++;

        struct spec sp = {.flags = 0, .width = -1, .precision = -1};

        /* Flags. */
        for (;; p++) {
            if (*p == '-') {
                sp.flags |= FLAG_LEFT;
            } else if (*p == '+') {
                sp.flags |= FLAG_PLUS;
            } else if (*p == ' ') {
                sp.flags |= FLAG_SPACE;
            } else if (*p == '0') {
                sp.flags |= FLAG_ZERO;
            } else if (*p == '#') {
                sp.flags |= FLAG_ALT;
            } else {
                break;
            }
        }

        /* Width. */
        if (*p == '*') {
            sp.width = va_arg(args, int);
            if (sp.width < 0) {
                sp.flags |= FLAG_LEFT;
                sp.width = (sp.width == INT32_MIN) ? INT32_MAX : -sp.width;
            }
            p++;
        } else {
            while (*p >= '0' && *p <= '9') {
                sp.width = ((sp.width < 0 ? 0 : sp.width) * 10) + (*p - '0');
                p++;
            }
        }

        /* Precision. */
        if (*p == '.') {
            p++;
            if (*p == '*') {
                sp.precision = va_arg(args, int);
                if (sp.precision < 0) {
                    sp.precision = -1; /* negative precision: as if omitted */
                }
                p++;
            } else {
                sp.precision = 0;
                while (*p >= '0' && *p <= '9') {
                    sp.precision = (sp.precision * 10) + (*p - '0');
                    p++;
                }
            }
        }

        /* Length modifier. */
        enum length_mod len = LEN_NONE;
        if (*p == 'h') {
            len = LEN_H;
            p++;
            if (*p == 'h') {
                len = LEN_HH;
                p++;
            }
        } else if (*p == 'l') {
            len = LEN_L;
            p++;
            if (*p == 'l') {
                len = LEN_LL;
                p++;
            }
        } else if (*p == 'z') {
            len = LEN_Z;
            p++;
        } else if (*p == 't') {
            len = LEN_T;
            p++;
        }

        switch (*p) {
        case 'd':
        case 'i': {
            int64_t v = arg_signed(&args, len);
            bool neg = v < 0;
            uint64_t mag = neg ? 0 - (uint64_t)v : (uint64_t)v;
            char sign = 0;
            if (neg) {
                sign = '-';
            } else if (sp.flags & FLAG_PLUS) {
                sign = '+';
            } else if (sp.flags & FLAG_SPACE) {
                sign = ' ';
            }
            emit_int(&s, &sp, mag, sign, "", 10, DIGITS_LOWER);
            break;
        }
        case 'u':
            emit_int(&s, &sp, arg_unsigned(&args, len), 0, "", 10, DIGITS_LOWER);
            break;
        case 'o': {
            uint64_t v = arg_unsigned(&args, len);
            /* '#': ensure a leading zero unless one is already produced. */
            const char *prefix = ((sp.flags & FLAG_ALT) && v != 0) ? "0" : "";
            emit_int(&s, &sp, v, 0, prefix, 8, DIGITS_LOWER);
            break;
        }
        case 'x': {
            uint64_t v = arg_unsigned(&args, len);
            const char *prefix = ((sp.flags & FLAG_ALT) && v != 0) ? "0x" : "";
            emit_int(&s, &sp, v, 0, prefix, 16, DIGITS_LOWER);
            break;
        }
        case 'X': {
            uint64_t v = arg_unsigned(&args, len);
            const char *prefix = ((sp.flags & FLAG_ALT) && v != 0) ? "0X" : "";
            emit_int(&s, &sp, v, 0, prefix, 16, DIGITS_UPPER);
            break;
        }
        case 'p': {
            /* Pointers always render as 0x-prefixed lowercase hex. */
            uint64_t v = (uint64_t)(uintptr_t)va_arg(args, void *);
            emit_int(&s, &sp, v, 0, "0x", 16, DIGITS_LOWER);
            break;
        }
        case 'c':
            emit_char(&s, &sp, (char)va_arg(args, int));
            break;
        case 's':
            emit_str(&s, &sp, va_arg(args, const char *));
            break;
        case '%':
            sink_putc(&s, '%');
            break;
        case '\0':
            /* Trailing lone '%': emit it literally and stop. */
            sink_putc(&s, '%');
            goto out;
        default:
            /* Unknown conversion: emit verbatim so bugs are visible in output. */
            sink_putc(&s, '%');
            sink_putc(&s, *p);
            break;
        }
    }

out:
    va_end(args);
    if (size > 0) {
        s.buf[(s.len < s.cap) ? s.len : s.cap] = '\0';
    }
    /* Lengths beyond INT32_MAX are unrepresentable; saturate. */
    return (s.len > (size_t)INT32_MAX) ? INT32_MAX : (int)s.len;
}

int snprintf(char *buf, size_t size, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return r;
}
