/*
 * panic.h - unrecoverable kernel error handling.
 *
 * Register dump and stack backtrace attach here once the IDT lands (Phase 2);
 * the reporting format is already stable: "PANIC: file:line: message".
 */
#ifndef HL_PANIC_H
#define HL_PANIC_H

#include <compiler.h>

NORETURN void panic_at(const char *file, int line, const char *fmt, ...) PRINTF(3, 4);

#define panic(...) panic_at(__FILE__, __LINE__, __VA_ARGS__)

#define KASSERT(cond)                                                                              \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            panic("assertion failed: %s", #cond);                                                  \
        }                                                                                          \
    } while (0)

#endif /* HL_PANIC_H */
