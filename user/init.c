/*
 * init.c - the first C userspace program.
 *
 * Freestanding (no libc yet), built with -mno-sse/-mno-red-zone: the
 * kernel does not save FPU/SSE state across context switches, and
 * syscalls/interrupts may run on this stack.
 *
 * Doubles as the acceptance test for the ELF64 loader and the syscall
 * ABI: it deliberately places state in .text, .rodata, .data, and
 * .bss (four PT_LOAD-covered sections with distinct permissions) and
 * verifies all of it, plus every syscall result the kernel can
 * produce today. The exit status is the report: 0 means every check
 * passed, any other value names the first failed check. The kernel
 * prints the status and the boot integration test asserts on it.
 */
#include <stdint.h>

#include "syscall.h"

static const char banner[] = "hello from ring 3\n";             /* .rodata */
static char report[] = "user: ? init: .data .bss .rodata ok\n"; /* .data  */
static volatile int data_word = 0x600D;                         /* .data  */
static volatile uint64_t bss_block[16];                         /* .bss   */

static unsigned long str_len(const char *s) {
    unsigned long n = 0;
    while (s[n] != '\0') {
        n++;
    }
    return n;
}

/* Called from crt0.asm; tidy cannot see assembly references. */
int main(void) { /* NOLINT(misc-use-internal-linkage) */
    /* write() returns the full length. */
    long n = (long)str_len(banner);
    if (sys_write(1, banner, str_len(banner)) != n) {
        return 1;
    }
    /* .bss arrived zero-filled (loader zeroed memsz > filesz). */
    for (unsigned i = 0; i < sizeof(bss_block) / sizeof(bss_block[0]); i++) {
        if (bss_block[i] != 0) {
            return 2;
        }
    }
    /* .data arrived initialized ... */
    if (data_word != 0x600D) {
        return 3;
    }
    /* ... and its pages are writable. */
    data_word = -1;
    if (data_word != -1) {
        return 4;
    }
    /* getpid() knows who we are. */
    if (sys_getpid() != 1) {
        return 5;
    }
    /* An unimplemented syscall reports -ENOSYS. */
    if (syscall0(999) != -ENOSYS) {
        return 6;
    }
    /* A bad fd reports -EBADF. */
    if (sys_write(7, banner, 1) != -EBADF) {
        return 7;
    }
    /* An unmapped user pointer reports -EFAULT, untouched. */
    if (sys_write(1, (const void *)0x1234000, 1) != -EFAULT) {
        return 8;
    }
    /* Patch the report in .data before printing it: proves the
     * write() source really is our mutable data segment. */
    report[6] = 'C';
    if (sys_write(1, report, str_len(report)) != (long)str_len(report)) {
        return 9;
    }
    return 0;
}
