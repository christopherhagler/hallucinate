/*
 * kprintf.h - formatted kernel console output.
 */
#ifndef HL_KPRINTF_H
#define HL_KPRINTF_H

#include <stdarg.h>

#include <compiler.h>

/* Format grammar: see fmt.h. Returns the number of characters written. */
int kprintf(const char *fmt, ...) PRINTF(1, 2);
int kvprintf(const char *fmt, va_list ap);

#endif /* HL_KPRINTF_H */
