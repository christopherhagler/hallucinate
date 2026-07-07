/*
 * init.c - the first C userspace program.
 *
 * Freestanding (no libc yet), built with -mno-sse/-mno-red-zone: the
 * kernel does not save FPU/SSE state across context switches, and
 * syscalls/interrupts may run on this stack.
 *
 * Doubles as the acceptance test for the ELF64 loader, the syscall
 * ABI, and the process model: it deliberately places state in .text,
 * .rodata, .data, and .bss (PT_LOAD-covered sections with distinct
 * permissions) and verifies all of it, verifies every syscall result
 * the kernel can produce today, forks a child that execs /bin/hello
 * and reaps it, and proves fault isolation — children that touch
 * kernel memory or execute an illegal instruction die with the right
 * signal while everything else keeps running. The exit status is the
 * report: 0 means every check passed, any other value names the
 * first failed check. The kernel prints the status and the boot
 * integration test asserts on it.
 */
#include <stdint.h>

#include "syscall.h"
#include "ulib.h"

static const char banner[] = "hello from ring 3\n";             /* .rodata */
static char report[] = "user: ? init: .data .bss .rodata ok\n"; /* .data  */
static volatile int data_word = 0x600D;                         /* .data  */
static volatile uint64_t bss_block[16];                         /* .bss   */

/*
 * The ABI promises that a syscall changes only rax (and rcx/r11 by
 * hardware). Prove it for every caller-saved argument register: the
 * "+" constraints make the compiler read the registers back instead
 * of assuming the asm preserved them.
 */
static int regs_survive_syscall(void) {
    long rdi = 0x1111;
    long rsi = 0x2222;
    long rdx = 0x3333;
    register long r10 __asm__("r10") = 0x4444;
    register long r8 __asm__("r8") = 0x5555;
    register long r9 __asm__("r9") = 0x6666;
    long ret;
    __asm__ volatile("syscall"
                     : "=a"(ret), "+D"(rdi), "+S"(rsi), "+d"(rdx), "+r"(r10), "+r"(r8), "+r"(r9)
                     : "a"((long)SYS_getpid)
                     : "rcx", "r11", "memory");
    return ret == 1 && rdi == 0x1111 && rsi == 0x2222 && rdx == 0x3333 && r10 == 0x4444 &&
           r8 == 0x5555 && r9 == 0x6666;
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
    /* Caller-saved registers survive a syscall (the kernel restores
     * the full set; only rax/rcx/r11 may change). */
    if (!regs_survive_syscall()) {
        return 10;
    }

    /* The process model: fork a child, replace its image with
     * /bin/hello via execve, and reap it. hello exits 42 only if its
     * argv arrived exactly as passed here. */
    long pid = sys_fork();
    if (pid < 0) {
        return 11;
    }
    if (pid == 0) {
        static const char *const args[] = {"/bin/hello", "via-fork-exec", 0};
        sys_execve("/bin/hello", args, 0);
        sys_exit(100); /* only reached if execve failed */
    }
    if (pid == sys_getpid()) {
        return 12; /* child pid must be a different process */
    }
    int wstatus = -1;
    if (sys_wait4(-1, &wstatus, 0) != pid) {
        return 13;
    }
    if (wstatus != (42 << 8)) {
        return 14; /* hello's checks failed or encoding is wrong */
    }
    /* No children left: waiting again reports -ECHILD. */
    if (sys_wait4(-1, 0, 0) != -ECHILD) {
        return 15;
    }
    /* execve of a nonexistent program fails cleanly. */
    if (sys_execve("/bin/nonesuch", 0, 0) != -2 /* -ENOENT */) {
        return 16;
    }

    /* Fault isolation: a child that pokes kernel memory dies with
     * SIGSEGV (11) — the kernel and everyone else keep running. */
    pid = sys_fork();
    if (pid < 0) {
        return 17;
    }
    if (pid == 0) {
        *(volatile long *)0xFFFFFFFF80000000 = 1; /* kernel image base */
        sys_exit(101);                            /* only reached if the fault did not kill us */
    }
    if (sys_wait4(-1, &wstatus, 0) != pid || wstatus != 11 /* SIGSEGV */) {
        return 18;
    }

    /* An illegal instruction dies with SIGILL (4). */
    pid = sys_fork();
    if (pid < 0) {
        return 19;
    }
    if (pid == 0) {
        __asm__ volatile("ud2");
        sys_exit(102); /* unreachable */
    }
    if (sys_wait4(-1, &wstatus, 0) != pid || wstatus != 4 /* SIGILL */) {
        return 20;
    }

    /* Patch the report in .data before printing it: proves the
     * write() source really is our mutable data segment. */
    report[6] = 'C';
    if (sys_write(1, report, str_len(report)) != (long)str_len(report)) {
        return 9;
    }
    return 0;
}
