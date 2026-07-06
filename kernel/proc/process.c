/*
 * process.c - the init process, from an embedded program image.
 *
 * User layout for the embedded init (one code page, one stack page):
 *
 *   0x0000000000400000  code+data, RX, from the blob (<= 4 KiB)
 *   0x00007FFFFFFFE000  stack page, RW + NX
 *   0x00007FFFFFFFF000  initial RSP (one past the stack page)
 *
 * The hosting kernel thread builds the address space, binds to it,
 * and drops to ring 3; it never returns to kernel C except through
 * syscalls, and SYS_exit ends it via thread_exit(). All user frames
 * and page tables are reclaimed by paging_user_destroy() after join —
 * the selftest-style accounting in process_run_init() asserts it.
 */
#include <process.h>

#include <stdint.h>

#include <arch/x86_64/paging.h>
#include <arch/x86_64/usermode.h>
#include <kprintf.h>
#include <memlayout.h>
#include <panic.h>
#include <pmm.h>
#include <sched.h>
#include <string.h>
#include <vmm.h>

#define USER_INIT_BASE  0x400000ull
#define USER_STACK_PAGE 0x00007FFFFFFFE000ull
#define USER_STACK_TOP  (USER_STACK_PAGE + PAGE_SIZE)

/* kernel/user_blob.asm wraps build/user/init.bin */
extern const uint8_t user_init_blob[];
extern const uint8_t user_init_blob_end[];

/* Integer subtraction: the analyzer cannot see that both linker
 * symbols delimit one object. */
static uint64_t blob_size_bytes(void) {
    return (uint64_t)((uintptr_t)user_init_blob_end - (uintptr_t)user_init_blob);
}

static struct {
    struct addrspace as;
    int exit_status;
    int exited;
} init_proc;

/* Allocate a frame, fill it from `src` (zero-padded), map it. */
static void map_filled_page(struct addrspace *as, uint64_t virt, const void *src, uint64_t len,
                            uint64_t flags) {
    KASSERT(len <= PAGE_SIZE);
    uint64_t frame = pmm_alloc_frame();
    if (frame == 0) {
        panic("process: out of frames for init");
    }
    uint8_t *page = phys_to_virt(frame);
    memset(page, 0, PAGE_SIZE);
    if (len > 0) {
        memcpy(page, src, len);
    }
    if (paging_map_4k(as, virt, frame, flags) != PAGING_OK) {
        panic("process: mapping %#llx for init failed", (unsigned long long)virt);
    }
}

static void init_host(void *arg) {
    (void)arg;
    uint64_t blob_size = blob_size_bytes();

    if (vmm_addrspace_create_user(&init_proc.as) != 0) {
        panic("process: no memory for init address space");
    }
    /* Code page: user, read + execute (no PTE_W, no PTE_NX). */
    map_filled_page(&init_proc.as, USER_INIT_BASE, user_init_blob, blob_size, PTE_US);
    /* Stack page: user, read + write, never executable. */
    map_filled_page(&init_proc.as, USER_STACK_PAGE, NULL, 0, PTE_US | PTE_W | PTE_NX);

    sched_set_addrspace(&init_proc.as);
    user_enter(USER_INIT_BASE, USER_STACK_TOP);
}

NORETURN void process_exit(int status) {
    init_proc.exit_status = status;
    init_proc.exited = 1;
    thread_exit();
}

void process_run_init(void) {
    uint64_t blob_size = blob_size_bytes();
    if (blob_size == 0 || blob_size > PAGE_SIZE) {
        panic("process: embedded init is %llu bytes (must be 1..4096)",
              (unsigned long long)blob_size);
    }
    uint64_t frames_before = pmm_free_frames();

    kprintf("user: launching init (embedded, %llu bytes)\n", (unsigned long long)blob_size);
    struct thread *host = thread_create("init", init_host, NULL);
    thread_join(host);

    KASSERT(init_proc.exited);
    paging_user_destroy(&init_proc.as);
    if (pmm_free_frames() != frames_before) {
        panic("process: init leaked %lld frames", (long long)(frames_before - pmm_free_frames()));
    }
    kprintf("user: init exited (status %d)\n", init_proc.exit_status);
}
