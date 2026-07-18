/*
 * init.c - the first C userspace program.
 *
 * Freestanding (no libc yet), built with -mno-sse/-mno-red-zone: the
 * kernel does not save FPU/SSE state across context switches, and
 * syscalls/interrupts may run on this stack.
 *
 * Doubles as the acceptance test for the ELF64 loader, the syscall
 * ABI, the process model, and the filesystem: it deliberately places
 * state in .text, .rodata, .data, and .bss (PT_LOAD-covered sections
 * with distinct permissions) and verifies all of it, verifies every
 * syscall result the kernel can produce today, forks a child that
 * execs /bin/hello and reaps it, proves fault isolation — children
 * that touch kernel memory or execute an illegal instruction die
 * with the right signal while everything else keeps running — and
 * exercises the whole VFS surface: the read side (open/read/close/
 * fstat/lseek/getdents64, devfs, path normalization) against the
 * known contents of the boot filesystem image, and the write path
 * (O_CREAT/O_EXCL/O_TRUNC/O_APPEND, write, mkdir/rmdir, link/unlink,
 * rename, fsync) in a scratch tree it removes without a trace. The
 * exit status is the report: 0 means every check passed, any other
 * value names the first failed check. The kernel prints the status
 * and the boot integration test asserts on it.
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

    /* --- the filesystem read side (slice 5c) --- */

    /* fds 0/1/2 arrived from the kernel as the console: a character
     * device, not a pipe-shaped placeholder. */
    struct stat st;
    if (sys_fstat(0, &st) != 0 || (st.st_mode & S_IFMT) != S_IFCHR) {
        return 21;
    }
    /* Open a known file; the lowest free fd slot is 3. */
    long fd = sys_open("/bin/hello", O_RDONLY);
    if (fd != 3) {
        return 22;
    }
    if (sys_fstat((int)fd, &st) != 0 || (st.st_mode & S_IFMT) != S_IFREG || st.st_size <= 0) {
        return 23;
    }
    /* Its bytes are really /bin/hello's: an ELF magic first. */
    char magic[4] = {0, 0, 0, 0};
    if (sys_read((int)fd, magic, 4) != 4) {
        return 24;
    }
    if (magic[0] != 0x7f || magic[1] != 'E' || magic[2] != 'L' || magic[3] != 'F') {
        return 25;
    }
    /* lseek: the end is st_size; rewinding rereads the magic. */
    if (sys_lseek((int)fd, 0, SEEK_END) != st.st_size) {
        return 26;
    }
    if (sys_lseek((int)fd, 0, SEEK_SET) != 0) {
        return 27;
    }
    char byte0 = 0;
    if (sys_read((int)fd, &byte0, 1) != 1 || byte0 != 0x7f) {
        return 28;
    }
    /* A read-only fd rejects write; a file rejects getdents64. */
    if (sys_write((int)fd, &byte0, 1) != -EBADF) {
        return 29;
    }
    char dbuf[512];
    if (sys_getdents64((int)fd, dbuf, sizeof(dbuf)) != -ENOTDIR) {
        return 30;
    }
    /* close() releases the fd; the number is then dead. */
    if (sys_close((int)fd) != 0) {
        return 31;
    }
    if (sys_close((int)fd) != -EBADF) {
        return 32;
    }

    /* Enumerate /bin: expect ".", "..", "init", "hello" and nothing
     * else (the manifest is the Makefile's fs.img rule). */
    long dfd = sys_open("/bin", O_RDONLY | O_DIRECTORY);
    if (dfd < 0) {
        return 33;
    }
    if (sys_read((int)dfd, dbuf, 1) != -EISDIR) {
        return 34;
    }
    long dn = sys_getdents64((int)dfd, dbuf, sizeof(dbuf));
    if (dn <= 0) {
        return 35;
    }
    unsigned seen = 0; /* bit per expected entry */
    for (long off = 0; off < dn;) {
        const struct dirent64 *d = (const struct dirent64 *)(dbuf + off);
        if (str_eq(d->d_name, ".") && d->d_type == DT_DIR) {
            seen |= 1u;
        } else if (str_eq(d->d_name, "..") && d->d_type == DT_DIR) {
            seen |= 2u;
        } else if (str_eq(d->d_name, "init") && d->d_type == DT_REG) {
            seen |= 4u;
        } else if (str_eq(d->d_name, "hello") && d->d_type == DT_REG) {
            seen |= 8u;
        } else {
            seen |= 16u; /* an entry nobody installed */
        }
        off += d->d_reclen;
    }
    if (seen != 15u) {
        return 36;
    }
    /* The cursor is at the end: the next call reports EOF. */
    if (sys_getdents64((int)dfd, dbuf, sizeof(dbuf)) != 0) {
        return 37;
    }
    if (sys_close((int)dfd) != 0) {
        return 38;
    }

    /* Path resolution corner cases. */
    if (sys_open("/bin/nonesuch", O_RDONLY) != -ENOENT) {
        return 39;
    }
    if (sys_open("/bin/hello/sub", O_RDONLY) != -ENOTDIR) {
        return 40;
    }
    if (sys_open("/bin/hello", O_RDONLY | O_DIRECTORY) != -ENOTDIR) {
        return 41;
    }
    /* A directory never opens for writing. */
    if (sys_open("/bin", O_WRONLY) != -EISDIR) {
        return 42;
    }
    /* "." and ".." normalize away (lexically, before resolution). */
    fd = sys_open("/dev/../bin/./hello", O_RDONLY);
    if (fd < 0) {
        return 43;
    }
    sys_close((int)fd);

    /* devfs: the console opens by path and is not seekable. */
    fd = sys_open("/dev/console", O_RDWR);
    if (fd < 0) {
        return 44;
    }
    if (sys_lseek((int)fd, 0, SEEK_SET) != -ESPIPE) {
        return 45;
    }
    static const char via[] = "user: console open via /dev/console ok\n";
    if (sys_write((int)fd, via, str_len(via)) != (long)str_len(via)) {
        return 46;
    }
    if (sys_close((int)fd) != 0) {
        return 47;
    }

    /* --- the write path (slice 5d) --- */

    /* mkdir, and EEXIST on repeat. */
    if (sys_mkdir("/t", 0755) != 0) {
        return 48;
    }
    if (sys_mkdir("/t", 0755) != -EEXIST) {
        return 49;
    }
    /* O_CREAT makes a file; the fd gives back exactly what we wrote. */
    static const char payload[] = "graphfs write path from ring 3";
    fd = sys_open3("/t/f", O_CREAT | O_EXCL | O_RDWR, 0644);
    if (fd < 0) {
        return 50;
    }
    if (sys_write((int)fd, payload, sizeof(payload)) != (long)sizeof(payload)) {
        return 51;
    }
    if (sys_lseek((int)fd, 0, SEEK_SET) != 0) {
        return 52;
    }
    char rbuf[64];
    if (sys_read((int)fd, rbuf, sizeof(rbuf)) != (long)sizeof(payload)) {
        return 53;
    }
    for (unsigned i = 0; i < sizeof(payload); i++) {
        if (rbuf[i] != payload[i]) {
            return 54;
        }
    }
    if (sys_fstat((int)fd, &st) != 0 || st.st_size != (long)sizeof(payload) || st.st_nlink != 1) {
        return 55;
    }
    /* fsync succeeds on a file (every write is already durable). */
    if (sys_fsync((int)fd) != 0) {
        return 56;
    }
    /* O_EXCL refuses an existing path. */
    if (sys_open3("/t/f", O_CREAT | O_EXCL | O_RDWR, 0644) != -EEXIST) {
        return 57;
    }
    if (sys_close((int)fd) != 0) {
        return 58;
    }
    /* O_APPEND lands writes at EOF. */
    fd = sys_open("/t/f", O_WRONLY | O_APPEND);
    if (fd < 0) {
        return 59;
    }
    if (sys_write((int)fd, "!", 1) != 1) {
        return 60;
    }
    if (sys_fstat((int)fd, &st) != 0 || st.st_size != (long)sizeof(payload) + 1) {
        return 61;
    }
    /* A write-only description refuses read... */
    if (sys_read((int)fd, rbuf, 1) != -EBADF) {
        return 62;
    }
    if (sys_close((int)fd) != 0) {
        return 63;
    }
    /* ...and a read-only one still refuses write (checked at 29). */

    /* link: two names, one file — nlink says so, bytes agree. */
    if (sys_link("/t/f", "/t/g") != 0) {
        return 64;
    }
    fd = sys_open("/t/g", O_RDONLY);
    if (fd < 0 || sys_fstat((int)fd, &st) != 0 || st.st_nlink != 2) {
        return 65;
    }
    if (sys_read((int)fd, rbuf, 4) != 4 || rbuf[0] != payload[0]) {
        return 66;
    }
    if (sys_close((int)fd) != 0) {
        return 67;
    }
    /* Dropping one name keeps the file alive under the other. */
    if (sys_unlink("/t/f") != 0 || sys_open("/t/f", O_RDONLY) != -ENOENT) {
        return 68;
    }
    /* The last name of an *open* file cannot be removed (v1 policy):
     * -EBUSY while open, gone after close. */
    fd = sys_open("/t/g", O_RDONLY);
    if (fd < 0) {
        return 69;
    }
    if (sys_unlink("/t/g") != -EBUSY) {
        return 70;
    }
    if (sys_close((int)fd) != 0 || sys_unlink("/t/g") != 0) {
        return 71;
    }
    /* O_TRUNC empties an existing file. */
    fd = sys_open3("/t/f2", O_CREAT | O_WRONLY, 0644);
    if (fd < 0 || sys_write((int)fd, payload, sizeof(payload)) != (long)sizeof(payload) ||
        sys_close((int)fd) != 0) {
        return 72;
    }
    fd = sys_open("/t/f2", O_WRONLY | O_TRUNC);
    if (fd < 0 || sys_fstat((int)fd, &st) != 0 || st.st_size != 0 || sys_close((int)fd) != 0) {
        return 73;
    }
    /* rename moves the file; the old name is gone. */
    if (sys_rename("/t/f2", "/t/f3") != 0 || sys_open("/t/f2", O_RDONLY) != -ENOENT) {
        return 74;
    }
    /* A directory cannot move into its own subtree. */
    if (sys_mkdir("/t/d", 0755) != 0) {
        return 75;
    }
    if (sys_rename("/t", "/t/d/sub") != -EINVAL) {
        return 76;
    }
    /* unlink on a directory / rmdir on a file: wrong tool, right errno. */
    if (sys_unlink("/t/d") != -EISDIR) {
        return 77;
    }
    if (sys_rmdir("/t/f3") != -ENOTDIR) {
        return 78;
    }
    /* devfs and mount points refuse mutation. */
    if (sys_mkdir("/dev/x", 0755) != -EPERM) {
        return 79;
    }
    if (sys_rmdir("/dev") != -EBUSY) {
        return 80;
    }
    if (sys_rename("/t/f3", "/dev/x") != -EXDEV) {
        return 81;
    }
    /* fsync on the console: a special file with nothing to sync. */
    if (sys_fsync(1) != -EINVAL) {
        return 82;
    }
    /* rmdir refuses a non-empty directory, then the cleanup empties
     * everything and the tree vanishes without a trace. */
    if (sys_rmdir("/t") != -ENOTEMPTY) {
        return 83;
    }
    if (sys_unlink("/t/f3") != 0 || sys_rmdir("/t/d") != 0 || sys_rmdir("/t") != 0) {
        return 84;
    }
    if (sys_open("/t", O_RDONLY | O_DIRECTORY) != -ENOENT) {
        return 85;
    }

    /* Patch the report in .data before printing it: proves the
     * write() source really is our mutable data segment. */
    report[6] = 'C';
    if (sys_write(1, report, str_len(report)) != (long)str_len(report)) {
        return 9;
    }
    return 0;
}
