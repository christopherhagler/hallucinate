/*
 * paging.c - x86_64 page table construction and walking.
 */
#include <arch/x86_64/paging.h>

#include <stddef.h>

#include <arch/x86_64/cpu.h>
#include <memlayout.h>
#include <pmm.h>
#include <string.h>

#define ENTRIES     512
#define SIZE_4K     0x1000ull
#define SIZE_2M     0x200000ull
#define TABLE_FLAGS (PTE_P | PTE_W) /* non-leaf entries: permissive; leaves decide */

static uint64_t *table_virt(uint64_t table_phys) {
    return phys_to_virt(table_phys);
}

static int index_at(uint64_t virt, int level) {
    /* level: 4 = PML4, 3 = PDPT, 2 = PD, 1 = PT */
    return (int)((virt >> (12 + (9 * (level - 1)))) & 0x1FF);
}

/* Return the phys addr of the next-level table, allocating it if the
 * entry is empty. 0 means out of memory. */
static uint64_t next_table(uint64_t table_phys, int index) {
    uint64_t *table = table_virt(table_phys);
    uint64_t entry = table[index];
    if (entry & PTE_P) {
        return entry & PTE_ADDR_MASK;
    }
    uint64_t frame = pmm_alloc_frame();
    if (frame == 0) {
        return 0;
    }
    memset(table_virt(frame), 0, SIZE_4K);
    table[index] = frame | TABLE_FLAGS;
    return frame;
}

int paging_addrspace_create(struct addrspace *as) {
    uint64_t frame = pmm_alloc_frame();
    if (frame == 0) {
        return PAGING_ENOMEM;
    }
    memset(table_virt(frame), 0, SIZE_4K);
    as->pml4_phys = frame;
    return PAGING_OK;
}

static int map_common(struct addrspace *as, uint64_t virt, uint64_t phys, uint64_t flags,
                      int leaf_level, uint64_t page_size) {
    if ((virt & (page_size - 1)) != 0 || (phys & (page_size - 1)) != 0) {
        return PAGING_EALIGN;
    }
    uint64_t table = as->pml4_phys;
    for (int level = 4; level > leaf_level; level--) {
        table = next_table(table, index_at(virt, level));
        if (table == 0) {
            return PAGING_ENOMEM;
        }
    }
    uint64_t *leaf = table_virt(table);
    int idx = index_at(virt, leaf_level);
    if (leaf[idx] & PTE_P) {
        return PAGING_EEXIST;
    }
    uint64_t ps = (leaf_level == 2) ? PTE_PS : 0;
    leaf[idx] = (phys & PTE_ADDR_MASK) | flags | ps | PTE_P;
    return PAGING_OK;
}

int paging_map_4k(struct addrspace *as, uint64_t virt, uint64_t phys, uint64_t flags) {
    return map_common(as, virt, phys, flags, 1, SIZE_4K);
}

int paging_map_2m(struct addrspace *as, uint64_t virt, uint64_t phys, uint64_t flags) {
    return map_common(as, virt, phys, flags, 2, SIZE_2M);
}

uint64_t paging_lookup(const struct addrspace *as, uint64_t virt, uint64_t *phys_out) {
    uint64_t table = as->pml4_phys;
    for (int level = 4; level >= 1; level--) {
        uint64_t entry = table_virt(table)[index_at(virt, level)];
        if (!(entry & PTE_P)) {
            return 0;
        }
        int is_leaf = (level == 1) || (level == 2 && (entry & PTE_PS));
        if (is_leaf) {
            uint64_t page_size = (level == 2) ? SIZE_2M : SIZE_4K;
            if (phys_out != NULL) {
                *phys_out = (entry & PTE_ADDR_MASK & ~(page_size - 1)) | (virt & (page_size - 1));
            }
            return entry;
        }
        table = entry & PTE_ADDR_MASK;
    }
    return 0; /* 1 GiB pages are not used */
}

void paging_activate(const struct addrspace *as) {
    write_cr3(as->pml4_phys);
}
