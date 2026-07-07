/*
 * process.c - the init process, from an embedded ELF64 image.
 *
 * User layout: the program's PT_LOAD segments land wherever the ELF
 * says (our user.ld links at 0x400000), and the stack sits just
 * below the canonical boundary:
 *
 *   0x00007FFFFFFFB000  stack, 4 pages RW + NX
 *   0x00007FFFFFFFF000  initial RSP (one past the stack)
 *
 * The hosting kernel thread builds the address space, binds to it,
 * and drops to ring 3; it never returns to kernel C except through
 * syscalls, and SYS_exit ends it via thread_exit(). All user frames
 * and page tables are reclaimed by paging_user_destroy() after join —
 * the frame accounting in process_run_init() asserts it.
 */
#include <process.h>

#include <stdint.h>

#include <arch/x86_64/paging.h>
#include <arch/x86_64/usermode.h>
#include <elf64.h>
#include <kprintf.h>
#include <memlayout.h>
#include <panic.h>
#include <pmm.h>
#include <sched.h>
#include <string.h>
#include <vmm.h>

#define USER_STACK_TOP   0x00007FFFFFFFF000ull
#define USER_STACK_PAGES 4u

/* kernel/user_blob.asm wraps build/user/init.elf */
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

/* Allocate a zeroed frame and map it at `virt`. */
static void map_zero_page(struct addrspace *as, uint64_t virt, uint64_t flags) {
    uint64_t frame = pmm_alloc_frame();
    if (frame == 0) {
        panic("process: out of frames for init");
    }
    memset(phys_to_virt(frame), 0, PAGE_SIZE);
    if (paging_map_4k(as, virt, frame, flags) != PAGING_OK) {
        panic("process: mapping %#llx for init failed", (unsigned long long)virt);
    }
}

static void init_host(void *arg) {
    (void)arg;
    if (vmm_addrspace_create_user(&init_proc.as) != 0) {
        panic("process: no memory for init address space");
    }
    uint64_t entry = 0;
    int err = elf64_load(&init_proc.as, user_init_blob, blob_size_bytes(), &entry);
    if (err != ELF64_OK) {
        panic("process: loading init failed: %s", elf64_strerror(err));
    }
    /* Stack pages: user, read + write, never executable. */
    for (unsigned i = 1; i <= USER_STACK_PAGES; i++) {
        map_zero_page(&init_proc.as, USER_STACK_TOP - ((uint64_t)i * PAGE_SIZE),
                      PTE_US | PTE_W | PTE_NX);
    }
    sched_set_addrspace(&init_proc.as);
    user_enter(entry, USER_STACK_TOP);
}

NORETURN void process_exit(int status) {
    init_proc.exit_status = status;
    init_proc.exited = 1;
    thread_exit();
}

void process_run_init(void) {
    uint64_t blob_size = blob_size_bytes();
    /* Fail before the thread launch if the image is unusable. */
    int err = elf64_validate(user_init_blob, blob_size, PAGE_SIZE, USER_VA_LIMIT, NULL);
    if (err != ELF64_OK) {
        panic("process: embedded init rejected: %s", elf64_strerror(err));
    }
    uint64_t frames_before = pmm_free_frames();

    kprintf("user: launching init (embedded ELF, %llu bytes)\n", (unsigned long long)blob_size);
    struct thread *host = thread_create("init", init_host, NULL);
    thread_join(host);

    KASSERT(init_proc.exited);
    paging_user_destroy(&init_proc.as);
    if (pmm_free_frames() != frames_before) {
        panic("process: init leaked %lld frames", (long long)(frames_before - pmm_free_frames()));
    }
    kprintf("user: init exited (status %d)\n", init_proc.exit_status);
}
