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

/*
 * Return the phys addr of the next-level table, allocating it if the
 * entry is empty. 0 means out of memory. User leaf mappings need
 * PTE_US at every level of the walk, so it propagates upward here;
 * the leaf entry still decides the effective permission.
 */
static uint64_t next_table(uint64_t table_phys, int index, uint64_t user_flag) {
    uint64_t *table = table_virt(table_phys);
    uint64_t entry = table[index];
    if (entry & PTE_P) {
        if (user_flag != 0 && (entry & PTE_US) == 0) {
            table[index] = entry | PTE_US;
        }
        return entry & PTE_ADDR_MASK;
    }
    uint64_t frame = pmm_alloc_frame();
    if (frame == 0) {
        return 0;
    }
    memset(table_virt(frame), 0, SIZE_4K);
    table[index] = frame | TABLE_FLAGS | user_flag;
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
        table = next_table(table, index_at(virt, level), flags & PTE_US);
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

int paging_user_clone(struct addrspace *dst, const struct addrspace *src) {
    const uint64_t *pml4 = table_virt(src->pml4_phys);
    for (int i4 = 0; i4 < ENTRIES / 2; i4++) { /* lower half only */
        if (!(pml4[i4] & PTE_P)) {
            continue;
        }
        const uint64_t *pdpt = table_virt(pml4[i4] & PTE_ADDR_MASK);
        for (int i3 = 0; i3 < ENTRIES; i3++) {
            if (!(pdpt[i3] & PTE_P)) {
                continue;
            }
            const uint64_t *pd = table_virt(pdpt[i3] & PTE_ADDR_MASK);
            for (int i2 = 0; i2 < ENTRIES; i2++) {
                if (!(pd[i2] & PTE_P)) {
                    continue;
                }
                if (pd[i2] & PTE_PS) {
                    return PAGING_EALIGN; /* user mappings are 4 KiB only */
                }
                const uint64_t *pt = table_virt(pd[i2] & PTE_ADDR_MASK);
                for (int i1 = 0; i1 < ENTRIES; i1++) {
                    if (!(pt[i1] & PTE_P)) {
                        continue;
                    }
                    uint64_t frame = pmm_alloc_frame();
                    if (frame == 0) {
                        return PAGING_ENOMEM;
                    }
                    memcpy(table_virt(frame), table_virt(pt[i1] & PTE_ADDR_MASK), SIZE_4K);
                    uint64_t virt = ((uint64_t)i4 << 39) | ((uint64_t)i3 << 30) |
                                    ((uint64_t)i2 << 21) | ((uint64_t)i1 << 12);
                    uint64_t flags = pt[i1] & (PTE_W | PTE_US | PTE_NX);
                    int rc = paging_map_4k(dst, virt, frame, flags);
                    if (rc != PAGING_OK) {
                        pmm_free_frame(frame);
                        return rc;
                    }
                }
            }
        }
    }
    return PAGING_OK;
}

void paging_user_destroy(struct addrspace *as) {
    uint64_t *pml4 = table_virt(as->pml4_phys);
    for (int i4 = 0; i4 < ENTRIES / 2; i4++) { /* lower half only */
        if (!(pml4[i4] & PTE_P)) {
            continue;
        }
        uint64_t pdpt_phys = pml4[i4] & PTE_ADDR_MASK;
        uint64_t *pdpt = table_virt(pdpt_phys);
        for (int i3 = 0; i3 < ENTRIES; i3++) {
            if (!(pdpt[i3] & PTE_P)) {
                continue;
            }
            uint64_t pd_phys = pdpt[i3] & PTE_ADDR_MASK;
            uint64_t *pd = table_virt(pd_phys);
            for (int i2 = 0; i2 < ENTRIES; i2++) {
                if (!(pd[i2] & PTE_P)) {
                    continue;
                }
                if (pd[i2] & PTE_PS) {
                    uint64_t base = pd[i2] & PTE_ADDR_MASK & ~(SIZE_2M - 1);
                    for (uint64_t off = 0; off < SIZE_2M; off += SIZE_4K) {
                        pmm_free_frame(base + off);
                    }
                    continue;
                }
                uint64_t pt_phys = pd[i2] & PTE_ADDR_MASK;
                uint64_t *pt = table_virt(pt_phys);
                for (int i1 = 0; i1 < ENTRIES; i1++) {
                    if (pt[i1] & PTE_P) {
                        pmm_free_frame(pt[i1] & PTE_ADDR_MASK);
                    }
                }
                pmm_free_frame(pt_phys);
            }
            pmm_free_frame(pd_phys);
        }
        pmm_free_frame(pdpt_phys);
    }
    pmm_free_frame(as->pml4_phys);
    as->pml4_phys = 0;
}
