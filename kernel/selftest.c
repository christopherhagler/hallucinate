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

#include <arch/x86_64/trap.h>
#include <compiler.h>
#include <fmt.h>
#include <kmalloc.h>
#include <kprintf.h>
#include <memlayout.h>
#include <panic.h>
#include <pmm.h>
#include <string.h>
#include <vmm.h>

#include <arch/x86_64/paging.h>

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

void selftest_run(void) {
    assertions = 0;
    test_string();
    test_fmt();
    test_traps();
    test_pmm();
    test_vmm();
    test_heap();
    kprintf("selftest: passed (%d assertions)\n", assertions);
}
