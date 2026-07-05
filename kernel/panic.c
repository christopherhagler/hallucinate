/*
 * panic.c - unrecoverable kernel error handling.
 */
#include <panic.h>

#include <stdarg.h>

#include <arch/x86_64/cpu.h>
#include <kprintf.h>

NORETURN void panic_at(const char *file, int line, const char *fmt, ...) {
    kprintf("\nPANIC: %s:%d: ", file, line);
    va_list ap;
    va_start(ap, fmt);
    kvprintf(fmt, ap);
    va_end(ap);
    kprintf("\nsystem halted\n");
    cpu_halt_forever();
}
