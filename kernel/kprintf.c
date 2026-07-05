/*
 * kprintf.c - formatted kernel console output.
 */
#include <kprintf.h>

#include <console.h>
#include <fmt.h>
#include <string.h>

/*
 * One line of kernel output. Messages longer than this are truncated and
 * marked; kernel log lines have no business being this long.
 */
#define KPRINTF_MAX 512

int kvprintf(const char *fmt, va_list ap) {
    char buf[KPRINTF_MAX];
    int want = vsnprintf(buf, sizeof(buf), fmt, ap);
    if (want < 0) {
        return 0;
    }
    size_t len = strnlen(buf, sizeof(buf) - 1);
    console_write(buf, len);
    if ((size_t)want > len) {
        static const char marker[] = " [kprintf: truncated]\n";
        console_write(marker, sizeof(marker) - 1);
    }
    return (int)len;
}

int kprintf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = kvprintf(fmt, ap);
    va_end(ap);
    return r;
}
