/*
 * fmt.h - freestanding printf-style formatting.
 *
 * C99 semantics: the return value is the length the fully formatted string
 * would have (excluding the terminating NUL); output is truncated to `size`
 * and always NUL-terminated when size > 0.
 *
 * Supported conversions: d i u o x X c s p %
 * Supported flags:       -  +  space  0  #
 * Width/precision:       decimal digits or *
 * Length modifiers:      hh h l ll z t
 */
#ifndef HL_FMT_H
#define HL_FMT_H

#include <stdarg.h>
#include <stddef.h>

#include <compiler.h>

int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);
int snprintf(char *buf, size_t size, const char *fmt, ...) PRINTF(3, 4);

#endif /* HL_FMT_H */
