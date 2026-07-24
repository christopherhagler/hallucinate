/*
 * selftest.c - in-kernel boot-time self-tests.
 *
 * A thin sanity layer proving the freestanding lib behaves in the real kernel
 * environment (higher half, no SSE, -mcmodel=kernel). The exhaustive suite
 * for the same code runs on the host under sanitizers (tests/host/).
 */
#include <selftest.h>

#include <stddef.h>
#include <stdint.h>

#include <arch/x86_64/cpu.h>
#include <arch/x86_64/trap.h>
#include <compiler.h>
#include <fmt.h>
#include <kmalloc.h>
#include <kprintf.h>
#include <memlayout.h>
#include <panic.h>
#include <pmm.h>
#include <sched.h>
#include <string.h>
#include <timer.h>
#include <vfs.h>
#include <vmm.h>

#include <arch/x86_64/paging.h>
#include <errno.h>

static int assertions;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        assertions++;                                                                              \
        if (!(cond)) {                                                                             \
            panic("selftest: %s", #cond);                                                          \
        }                                                                                          \
    } while (0)

static void check_fmt(const char *expected, const char *got) {
    assertions++;
    if (strcmp(expected, got) != 0) {
        panic("selftest: fmt: expected \"%s\", got \"%s\"", expected, got);
    }
}

static void test_string(void) {
    uint8_t a[16];
    uint8_t b[16];
    memset(a, 0x5A, sizeof(a));
    memcpy(b, a, sizeof(a));
    CHECK(memcmp(a, b, sizeof(a)) == 0);

    char buf[9];
    memcpy(buf, "abcdefgh", 9);
    memmove(buf + 2, buf, 6);
    CHECK(memcmp(buf, "ababcdef", 8) == 0);

    CHECK(strlen("hallucinate") == 11);
    CHECK(strnlen("hallucinate", 5) == 5);
    CHECK(strcmp("boot", "boot") == 0);
    CHECK(strncmp("kernel", "kernfs", 4) == 0);
}

static void test_fmt(void) {
    char buf[64];

    snprintf(buf, sizeof(buf), "%d %u %x", -42, 42u, 0xBEEFu);
    check_fmt("-42 42 beef", buf);

    snprintf(buf, sizeof(buf), "%#018llx", 0xffffffff80100000ull);
    check_fmt("0xffffffff80100000", buf); /* exactly 18 wide incl. 0x */

    snprintf(buf, sizeof(buf), "%5s|%-5s|%.2s", "ab", "cd", "efgh");
    check_fmt("   ab|cd   |ef", buf);

    snprintf(buf, sizeof(buf), "%p", (void *)0x6000);
    check_fmt("0x6000", buf);

    int want = snprintf(buf, 4, "%s", "truncated");
    CHECK(want == 9);
    check_fmt("tru", buf);
}

static int breakpoints_taken;

static void breakpoint_handler(struct trapframe *tf) {
    /* #BP is a trap: RIP already points past the int3. */
    breakpoints_taken++;
    assertions++;
    if (tf->vector != VEC_BREAKPOINT) {
        panic("selftest: breakpoint handler got vector %llu", (unsigned long long)tf->vector);
    }
}

static void test_traps(void) {
    breakpoints_taken = 0;
    trap_handler_t prev = trap_register(VEC_BREAKPOINT, breakpoint_handler);
    CHECK(prev == NULL);
    __asm__ volatile("int3");
    CHECK(breakpoints_taken == 1);
    trap_register(VEC_BREAKPOINT, prev);
}

static void test_pmm(void) {
    uint64_t free_before = pmm_free_frames();
    CHECK(free_before > 0);

    /* Allocate a batch of frames: page-aligned, distinct, writable. */
    enum { BATCH = 32 };
    uint64_t frames[BATCH];
    for (int i = 0; i < BATCH; i++) {
        frames[i] = pmm_alloc_frame();
        CHECK(frames[i] != 0);
        CHECK((frames[i] & (PAGE_SIZE - 1)) == 0);
        CHECK(frames[i] < BOOT_MAPPED_LIMIT);
        for (int j = 0; j < i; j++) {
            CHECK(frames[i] != frames[j]);
        }
        /* Prove the frame is real, mapped memory. */
        uint8_t *mem = phys_to_virt(frames[i]);
        memset(mem, 0xA5 + i, PAGE_SIZE);
        CHECK(mem[0] == (uint8_t)(0xA5 + i) && mem[PAGE_SIZE - 1] == (uint8_t)(0xA5 + i));
    }
    CHECK(pmm_free_frames() == free_before - BATCH);

    /* Contiguous run, 8 frames aligned to 8. */
    uint64_t run = pmm_alloc_frames(8, 8);
    CHECK(run != 0);
    CHECK((run & ((8 * PAGE_SIZE) - 1)) == 0);

    for (int i = 0; i < BATCH; i++) {
        pmm_free_frame(frames[i]);
    }
    for (int i = 0; i < 8; i++) {
        pmm_free_frame(run + ((uint64_t)i * PAGE_SIZE));
    }
    CHECK(pmm_free_frames() == free_before);
}

static void test_vmm(void) {
    extern char _text_start[];
    extern char _rodata_start[];
    extern char _data_start[];

    /* W^X on the kernel image: text executable and read-only, rodata
     * NX and read-only, data NX and writable. */
    uint64_t pte = vmm_kernel_lookup((uint64_t)_text_start, NULL);
    CHECK((pte & PTE_P) != 0 && (pte & PTE_W) == 0 && (pte & PTE_NX) == 0);
    pte = vmm_kernel_lookup((uint64_t)_rodata_start, NULL);
    CHECK((pte & PTE_P) != 0 && (pte & PTE_W) == 0 && (pte & PTE_NX) != 0);
    pte = vmm_kernel_lookup((uint64_t)_data_start, NULL);
    CHECK((pte & PTE_P) != 0 && (pte & PTE_W) != 0 && (pte & PTE_NX) != 0);

    /* The HHDM translates back to the physical address. */
    uint64_t phys = 0;
    pte = vmm_kernel_lookup(HHDM_BASE + 0x6000, &phys);
    CHECK((pte & PTE_P) != 0 && phys == 0x6000);
    CHECK((pte & PTE_NX) != 0); /* no code ever runs from the HHDM */

    /* The null page and the old identity map are gone. */
    CHECK(vmm_kernel_lookup(0, NULL) == 0);
    CHECK(vmm_kernel_lookup(0x100000, NULL) == 0);

    /* The bootinfo block is still readable through the new tables. */
    const struct bootinfo *bi = phys_to_virt(0x6000);
    CHECK(bi->magic == BOOTINFO_MAGIC);
}

static void test_heap(void) {
    uint64_t objects_before = kmalloc_live_objects();
    uint64_t pages_before = kmalloc_live_pages();
    uint64_t frames_before = pmm_free_frames();

    /* A spread of sizes across classes and into the large path. */
    static const uint32_t sizes[] = {1, 16, 64, 200, 1024, 1500, 5000, 3 * 4096};
    void *ptrs[ARRAY_SIZE(sizes)];
    for (uint32_t i = 0; i < ARRAY_SIZE(sizes); i++) {
        ptrs[i] = kmalloc(sizes[i]);
        CHECK(ptrs[i] != NULL);
        CHECK(((uintptr_t)ptrs[i] & 0xF) == 0);
        memset(ptrs[i], (int)(0x30 + i), sizes[i]);
    }
    for (uint32_t i = 0; i < ARRAY_SIZE(sizes); i++) {
        const uint8_t *b = ptrs[i];
        CHECK(b[0] == 0x30 + i && b[sizes[i] - 1] == 0x30 + i);
    }
    CHECK(kmalloc_live_objects() > objects_before);

    void *z = kzalloc(300);
    CHECK(z != NULL);
    const uint8_t *zb = z;
    CHECK(zb[0] == 0 && zb[150] == 0 && zb[299] == 0);
    kfree(z);

    for (uint32_t i = 0; i < ARRAY_SIZE(sizes); i++) {
        kfree(ptrs[i]);
    }
    CHECK(kmalloc_live_objects() == objects_before);
    CHECK(kmalloc_live_pages() == pages_before);
    CHECK(pmm_free_frames() == frames_before);
}

/* --- scheduler --- */

enum { INTERLEAVE_WORKERS = 3, INTERLEAVE_ROUNDS = 4 };
static char interleave_log[(INTERLEAVE_WORKERS * INTERLEAVE_ROUNDS) + 1];
static int interleave_pos;

static void interleave_worker(void *arg) {
    char tag = (char)(uintptr_t)arg;
    for (int i = 0; i < INTERLEAVE_ROUNDS; i++) {
        uint64_t flags = cpu_irq_save();
        if (interleave_pos < (int)sizeof(interleave_log) - 1) {
            interleave_log[interleave_pos++] = tag;
        }
        cpu_irq_restore(flags);
        sched_yield();
    }
}

static volatile int spinner_stop;
static volatile uint64_t spinner_count;

static void spinner_worker(void *arg) {
    (void)arg;
    while (!spinner_stop) {
        spinner_count++;
    }
}

static void sleeper_worker(void *arg) {
    uint64_t *slept = arg;
    uint64_t before = timer_ticks();
    sched_sleep_ticks(3);
    *slept = timer_ticks() - before;
}

static void test_sched(void) {
    uint64_t threads_before = sched_thread_count();
    uint64_t objects_before = kmalloc_live_objects();

    /*
     * Interleave proof: three workers each log their tag then yield,
     * four rounds. FIFO round-robin guarantees no worker logs twice
     * in a row (a rare timer preempt can rotate the order, but never
     * makes two appends by one worker adjacent).
     */
    interleave_pos = 0;
    struct thread *w[INTERLEAVE_WORKERS];
    /* Create all three atomically: a preempt between creates could
     * otherwise let the first worker run two rounds unopposed. */
    uint64_t flags = cpu_irq_save();
    w[0] = thread_create("ilv-a", interleave_worker, (void *)(uintptr_t)'a');
    w[1] = thread_create("ilv-b", interleave_worker, (void *)(uintptr_t)'b');
    w[2] = thread_create("ilv-c", interleave_worker, (void *)(uintptr_t)'c');
    cpu_irq_restore(flags);
    for (int i = 0; i < INTERLEAVE_WORKERS; i++) {
        thread_join(w[i]);
    }
    interleave_log[interleave_pos] = '\0';
    CHECK(interleave_pos == INTERLEAVE_WORKERS * INTERLEAVE_ROUNDS);
    int counts[INTERLEAVE_WORKERS] = {0};
    for (int i = 0; i < interleave_pos; i++) {
        counts[interleave_log[i] - 'a']++;
        if (i > 0) {
            CHECK(interleave_log[i] != interleave_log[i - 1]);
        }
    }
    for (int i = 0; i < INTERLEAVE_WORKERS; i++) {
        CHECK(counts[i] == INTERLEAVE_ROUNDS);
    }
    kprintf("selftest: sched interleave \"%s\"\n", interleave_log);

    /* Timed sleep blocks for at least the requested ticks. */
    uint64_t slept = 0;
    struct thread *sleeper = thread_create("sleeper", sleeper_worker, &slept);
    thread_join(sleeper);
    CHECK(slept >= 3);

    /*
     * Preemption proof: a worker that never yields cannot monopolize
     * the CPU — the timer tick puts us back on it. Reaching the
     * post-sleep CHECKs at all is the proof; the counter shows the
     * spinner really ran meanwhile.
     */
    spinner_stop = 0;
    spinner_count = 0;
    struct thread *spinner = thread_create("spinner", spinner_worker, NULL);
    sched_sleep_ticks(3);
    CHECK(spinner_count > 0);
    spinner_stop = 1;
    thread_join(spinner);

    /* Threads, TCBs, and stacks are all accounted for after joins. */
    CHECK(sched_thread_count() == threads_before);
    CHECK(kmalloc_live_objects() == objects_before);
}

/* --- pipes --- */

struct pipe_write_ctx {
    struct file *f;
    const uint8_t *data;
    size_t len;
    long result;
};

static void pipe_write_worker(void *arg) {
    struct pipe_write_ctx *ctx = arg;
    ctx->result = ctx->f->ops->write(ctx->f, ctx->data, ctx->len);
    vfs_file_put(ctx->f);
}

/*
 * Proves the part pipe_core.c's host tests cannot: a real writer
 * thread and a real reader thread, several times the buffer's
 * capacity apart, must round-trip every byte through actual
 * sched_block()/sched_wake() handoffs — not just the ring math.
 */
static void test_pipe(void) {
    uint64_t threads_before = sched_thread_count();
    uint64_t objects_before = kmalloc_live_objects();

    struct file *rf = NULL;
    struct file *wf = NULL;
    CHECK(pipe_open(&rf, &wf) == 0);

    /* Several times the pipe's internal buffer, so the writer is
     * forced to block on a full buffer at least once and rely on the
     * reader's wakeup, not just fit in one write() call. */
    enum { DATA_LEN = (4096 * 3) + 123 };
    static uint8_t src[DATA_LEN];
    static uint8_t dst[DATA_LEN];
    uint32_t seed = 0xC0FFEEu;
    for (size_t i = 0; i < DATA_LEN; i++) {
        seed = (seed * 1103515245u) + 12345u;
        src[i] = (uint8_t)(seed >> 24);
    }

    struct pipe_write_ctx ctx = {.f = wf, .data = src, .len = DATA_LEN, .result = -1};
    struct thread *writer = thread_create("pipe-writer", pipe_write_worker, &ctx);

    size_t got = 0;
    while (got < DATA_LEN) {
        long n = rf->ops->read(rf, dst + got, DATA_LEN - got);
        CHECK(n > 0); /* the writer is still alive; EOF here is a bug */
        got += (size_t)n;
    }
    /* The writer has (or is about to have) closed its end: this read
     * must resolve to EOF, not block forever. */
    CHECK(rf->ops->read(rf, dst, sizeof(dst)) == 0);

    thread_join(writer);
    CHECK(ctx.result == DATA_LEN);
    CHECK(memcmp(src, dst, DATA_LEN) == 0);
    vfs_file_put(rf);

    CHECK(sched_thread_count() == threads_before);
    CHECK(kmalloc_live_objects() == objects_before);

    /* -EPIPE: writing with no readers left, no blocking involved. */
    struct file *rf2 = NULL;
    struct file *wf2 = NULL;
    CHECK(pipe_open(&rf2, &wf2) == 0);
    vfs_file_put(rf2);
    uint8_t one = 'x';
    CHECK(wf2->ops->write(wf2, &one, 1) == -EPIPE);
    vfs_file_put(wf2);

    CHECK(kmalloc_live_objects() == objects_before);
    kprintf("selftest: pipes ok (blocking write/read, EOF, EPIPE)\n");
}

/* --- local sockets --- */

/*
 * Proves what test_pipe() proves for pipes (a real blocking handoff
 * across sched_block()/sched_wake(), EOF, -EPIPE), plus the part
 * unique to socketpair: both ends are simultaneously readable and
 * writable (full duplex), not split into separate read/write
 * descriptions the way pipe(2)'s two fds are.
 */
static void test_socket(void) {
    uint64_t threads_before = sched_thread_count();
    uint64_t objects_before = kmalloc_live_objects();

    struct file *fa = NULL;
    struct file *fb = NULL;
    CHECK(socketpair_open(AF_UNIX, SOCK_STREAM, 0, &fa, &fb) == 0);

    /* Full duplex, no blocking involved: each end writes, the other
     * reads its own, independent direction. */
    static const char msg_a[] = "a-to-b";
    static const char msg_b[] = "b-to-a-and-back";
    CHECK(fa->ops->write(fa, msg_a, sizeof(msg_a)) == (long)sizeof(msg_a));
    CHECK(fb->ops->write(fb, msg_b, sizeof(msg_b)) == (long)sizeof(msg_b));
    {
        char buf[sizeof(msg_a)];
        CHECK(fb->ops->read(fb, buf, sizeof(buf)) == (long)sizeof(buf));
        CHECK(memcmp(buf, msg_a, sizeof(msg_a)) == 0);
    }
    {
        char buf[sizeof(msg_b)];
        CHECK(fa->ops->read(fa, buf, sizeof(buf)) == (long)sizeof(buf));
        CHECK(memcmp(buf, msg_b, sizeof(msg_b)) == 0);
    }

    /* Real blocking handoff, several times a channel's capacity: a
     * writer thread on one end, this thread reading the other —
     * pipe_write_worker/pipe_write_ctx are generic over struct file,
     * so test_pipe()'s helpers serve here unchanged. */
    enum { DATA_LEN = (4096 * 3) + 77 };
    static uint8_t src[DATA_LEN];
    static uint8_t dst[DATA_LEN];
    uint32_t seed = 0xFACADEu;
    for (size_t i = 0; i < DATA_LEN; i++) {
        seed = (seed * 1103515245u) + 12345u;
        src[i] = (uint8_t)(seed >> 24);
    }
    struct pipe_write_ctx ctx = {.f = fa, .data = src, .len = DATA_LEN, .result = -1};
    struct thread *writer = thread_create("sock-writer", pipe_write_worker, &ctx);

    size_t got = 0;
    while (got < DATA_LEN) {
        long n = fb->ops->read(fb, dst + got, DATA_LEN - got);
        CHECK(n > 0); /* the writer is still alive; EOF here is a bug */
        got += (size_t)n;
    }
    thread_join(writer);
    CHECK(ctx.result == DATA_LEN);
    CHECK(memcmp(src, dst, DATA_LEN) == 0);

    /* pipe_write_worker already dropped fa's only reference: fb's read
     * must resolve to EOF now, not block forever. */
    CHECK(fb->ops->read(fb, dst, sizeof(dst)) == 0);
    vfs_file_put(fb);

    CHECK(sched_thread_count() == threads_before);
    CHECK(kmalloc_live_objects() == objects_before);

    /* -EPIPE: writing with the peer already gone, no blocking involved. */
    struct file *fa2 = NULL;
    struct file *fb2 = NULL;
    CHECK(socketpair_open(AF_UNIX, SOCK_STREAM, 0, &fa2, &fb2) == 0);
    vfs_file_put(fb2);
    uint8_t one = 'x';
    CHECK(fa2->ops->write(fa2, &one, 1) == -EPIPE);
    vfs_file_put(fa2);

    /* Unsupported domain/type/protocol are rejected before anything is
     * allocated. */
    struct file *rej_a = NULL;
    struct file *rej_b = NULL;
    CHECK(socketpair_open(2 /* not AF_UNIX */, SOCK_STREAM, 0, &rej_a, &rej_b) == -EAFNOSUPPORT);
    CHECK(socketpair_open(AF_UNIX, 2 /* not SOCK_STREAM */, 0, &rej_a, &rej_b) == -EPROTONOSUPPORT);
    CHECK(socketpair_open(AF_UNIX, SOCK_STREAM, 1, &rej_a, &rej_b) == -EPROTONOSUPPORT);

    CHECK(kmalloc_live_objects() == objects_before);
    kprintf("selftest: sockets ok (full duplex, blocking write/read, EOF, EPIPE)\n");
}

/* --- the filesystem write path --- */

/*
 * The boot-time stress for slice 5d: create/write/read/rename/unlink
 * cycles through the real VFS onto the real disk, with the same
 * leak discipline as the pmm/heap tests — after each cycle the
 * filesystem must hold exactly as many free blocks and nodes as
 * before it, and fsck verifies the resulting image after every boot
 * test (tests/run_qemu.py). Runs before userspace exists, in thread
 * context (vfs takes sleeping locks).
 */
static void test_fs(void) {
    uint64_t gen0;
    uint64_t blocks0;
    uint64_t nodes0;
    vfs_stats(&gen0, &blocks0, &nodes0);

    for (int round = 0; round < 2; round++) {
        CHECK(vfs_mkdir("/selftest", 0755) == 0);
        CHECK(vfs_mkdir("/selftest", 0755) == -EEXIST);

        /* Files at awkward sizes around the block boundary, written
         * and read back through open file descriptions. */
        static const size_t sizes[] = {1, 4095, 4096, 4097, 100000};
        for (unsigned i = 0; i < ARRAY_SIZE(sizes); i++) {
            char path[32];
            snprintf(path, sizeof(path), "/selftest/f%u", i);
            struct file *f = NULL;
            CHECK(vfs_open(path, O_CREAT | O_EXCL | O_RDWR, 0644, &f) == 0);
            uint8_t *buf = kmalloc(sizes[i]);
            uint8_t *got = kzalloc(sizes[i]);
            CHECK(buf != NULL && got != NULL);
            for (size_t j = 0; j < sizes[i]; j++) {
                buf[j] = (uint8_t)((j * 13) + i + 1);
            }
            CHECK(f->ops->write(f, buf, sizes[i]) == (long)sizes[i]);
            CHECK(f->ops->lseek(f, 0, SEEK_SET) == 0);
            CHECK(f->ops->read(f, got, sizes[i]) == (long)sizes[i]);
            CHECK(memcmp(buf, got, sizes[i]) == 0);
            struct stat st;
            CHECK(f->ops->fstat(f, &st) == 0);
            CHECK(st.st_size == (int64_t)sizes[i]);
            CHECK(f->ops->fsync(f) == 0);
            kfree(buf);
            kfree(got);
            vfs_file_put(f);
        }

        /* A read-only description refuses to write. */
        struct file *ro = NULL;
        CHECK(vfs_open("/selftest/f3", O_RDONLY, 0, &ro) == 0);
        CHECK(ro->ops->write(ro, "x", 1) == -EBADF);
        vfs_file_put(ro);

        /* Mutation respects the mount table. */
        CHECK(vfs_rmdir("/dev") == -EBUSY);
        CHECK(vfs_mkdir("/dev/x", 0755) == -EPERM);
        CHECK(vfs_rename("/selftest/f3", "/dev/f3") == -EXDEV);

        /* Rename (plain and replacing), then tear everything down. */
        CHECK(vfs_rmdir("/selftest") == -ENOTEMPTY);
        CHECK(vfs_rename("/selftest/f0", "/selftest/renamed") == 0);
        CHECK(vfs_unlink("/selftest/f0") == -ENOENT);
        CHECK(vfs_rename("/selftest/f1", "/selftest/f2") == 0); /* replaces f2 */
        CHECK(vfs_unlink("/selftest/renamed") == 0);
        CHECK(vfs_unlink("/selftest/f2") == 0);
        CHECK(vfs_unlink("/selftest/f3") == 0);
        CHECK(vfs_unlink("/selftest/f4") == 0);

        /* An open file's last name cannot be removed (the v1 -EBUSY
         * policy); after the close it can. */
        struct file *busy = NULL;
        CHECK(vfs_open("/selftest/busy", O_CREAT | O_WRONLY, 0600, &busy) == 0);
        CHECK(vfs_unlink("/selftest/busy") == -EBUSY);
        vfs_file_put(busy);
        CHECK(vfs_unlink("/selftest/busy") == 0);
        CHECK(vfs_rmdir("/selftest") == 0);

        /* Every block and node came back; the generation advanced. */
        uint64_t gen1;
        uint64_t blocks1;
        uint64_t nodes1;
        vfs_stats(&gen1, &blocks1, &nodes1);
        CHECK(blocks1 == blocks0);
        CHECK(nodes1 == nodes0);
        CHECK(gen1 > gen0);
    }
    kprintf("selftest: fs write path ok (create/write/rename/unlink cycles)\n");
}

void selftest_run(void) {
    assertions = 0;
    test_string();
    test_fmt();
    test_traps();
    test_pmm();
    test_vmm();
    test_heap();
    test_sched();
    test_pipe();
    test_socket();
    if (vfs_has_root()) {
        test_fs();
    } else {
        kprintf("selftest: fs write-path test skipped (no root filesystem)\n");
    }
    kprintf("selftest: passed (%d assertions)\n", assertions);
}
