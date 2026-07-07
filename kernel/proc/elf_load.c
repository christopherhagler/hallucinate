/*
 * elf_load.c - materialize a validated ELF64 executable into a user
 * address space.
 *
 * Every PT_LOAD segment gets fresh zeroed frames (so bss and segment
 * padding are zero-filled), the file bytes copied in page by page,
 * and leaf permissions derived from the segment flags: PF_X clears
 * NX, PF_W sets writable, the validator has already rejected W+X.
 *
 * All bounds and overflow checks happened in elf64_validate() — the
 * arithmetic here relies on that contract, and the only runtime
 * failure left is frame exhaustion. On ELF64_ENOMEM the address
 * space may be partially populated; the caller tears it down with
 * paging_user_destroy().
 */
#include <elf64.h>

#include <arch/x86_64/paging.h>
#include <memlayout.h>
#include <panic.h>
#include <pmm.h>
#include <string.h>

static uint64_t page_down(uint64_t addr) {
    return addr & ~(uint64_t)(PAGE_SIZE - 1);
}

static uint64_t seg_pte_flags(uint32_t p_flags) {
    uint64_t flags = PTE_US;
    if (p_flags & PF_W) {
        flags |= PTE_W;
    }
    if (!(p_flags & PF_X)) {
        flags |= PTE_NX;
    }
    return flags;
}

/* Copy the slice of the segment's file bytes that lands in the page
 * at `page_va` into the frame's direct-map view. */
static void fill_page(uint8_t *dst, uint64_t page_va, const uint8_t *image,
                      const struct elf64_phdr *ph) {
    uint64_t lo = page_va > ph->p_vaddr ? page_va : ph->p_vaddr;
    uint64_t file_end = ph->p_vaddr + ph->p_filesz;
    uint64_t hi = page_va + PAGE_SIZE < file_end ? page_va + PAGE_SIZE : file_end;
    if (hi > lo) {
        memcpy(dst + (lo - page_va), image + ph->p_offset + (lo - ph->p_vaddr), hi - lo);
    }
}

int elf64_load(struct addrspace *as, const uint8_t *image, uint64_t size, uint64_t *entry_out) {
    struct elf64_info info;
    int err = elf64_validate(image, size, PAGE_SIZE, USER_VA_LIMIT, &info);
    if (err != ELF64_OK) {
        return err;
    }

    for (uint16_t i = 0; i < info.phnum; i++) {
        struct elf64_phdr ph;
        elf64_phdr_get(image, i, &ph);
        if (ph.p_type != PT_LOAD || ph.p_memsz == 0) {
            continue;
        }
        uint64_t flags = seg_pte_flags(ph.p_flags);
        uint64_t seg_end = ph.p_vaddr + ph.p_memsz;
        for (uint64_t page = page_down(ph.p_vaddr); page < seg_end; page += PAGE_SIZE) {
            uint64_t frame = pmm_alloc_frame();
            if (frame == 0) {
                return ELF64_ENOMEM;
            }
            uint8_t *dst = phys_to_virt(frame);
            memset(dst, 0, PAGE_SIZE);
            fill_page(dst, page, image, &ph);
            int rc = paging_map_4k(as, page, frame, flags);
            if (rc == PAGING_ENOMEM) {
                pmm_free_frame(frame);
                return ELF64_ENOMEM;
            }
            /* EEXIST/EALIGN are impossible on a validated image. */
            KASSERT(rc == PAGING_OK);
        }
    }

    *entry_out = info.entry;
    return ELF64_OK;
}
