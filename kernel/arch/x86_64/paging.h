/*
 * paging.h - x86_64 4-level page table construction and walking.
 *
 * Operates on address spaces rooted at a PML4 frame. Table frames
 * come from the PMM and are touched through phys_to_virt(), so the
 * direct map (boot 1 GiB alias or the real HHDM) must cover them.
 *
 * Mapping over an existing mapping is an error (PAGING_EEXIST), not
 * an overwrite: silent remaps hide double-mapping bugs.
 */
#pragma once

#include <stdint.h>

/* PTE bits (Intel SDM Vol. 3, 4.5). */
#define PTE_P   (1ull << 0)  /* present */
#define PTE_W   (1ull << 1)  /* writable */
#define PTE_US  (1ull << 2)  /* user accessible */
#define PTE_PWT (1ull << 3)  /* write-through */
#define PTE_PCD (1ull << 4)  /* cache disable */
#define PTE_PS  (1ull << 7)  /* 2 MiB page (in a PDE) */
#define PTE_G   (1ull << 8)  /* global */
#define PTE_NX  (1ull << 63) /* no-execute (requires EFER.NXE) */

#define PTE_ADDR_MASK 0x000FFFFFFFFFF000ull

#define PAGING_OK     0
#define PAGING_ENOMEM (-1) /* PMM could not supply a table frame */
#define PAGING_EEXIST (-2) /* range already mapped */
#define PAGING_EALIGN (-3) /* address not aligned for the page size */

struct addrspace {
    uint64_t pml4_phys;
};

/* Allocate and zero a PML4. Returns PAGING_OK or PAGING_ENOMEM. */
int paging_addrspace_create(struct addrspace *as);

/* Map one 4 KiB / 2 MiB page. `flags` are PTE bits except the address
 * (PTE_P is implied). Intermediate tables are created as needed. */
int paging_map_4k(struct addrspace *as, uint64_t virt, uint64_t phys, uint64_t flags);
int paging_map_2m(struct addrspace *as, uint64_t virt, uint64_t phys, uint64_t flags);

/*
 * Walk to the leaf entry covering `virt`.
 * Returns the raw PTE/PDE (present bit set) or 0 if unmapped; stores
 * the translated physical address in *phys_out when non-NULL.
 */
uint64_t paging_lookup(const struct addrspace *as, uint64_t virt, uint64_t *phys_out);

/* Load CR3 with this address space. */
void paging_activate(const struct addrspace *as);

/*
 * Tear down the user (lower) half of an address space: free every
 * mapped frame and every page table under PML4 entries 0-255, then
 * the PML4 itself. Contract: every user-mapped frame is owned by this
 * address space (true until shared mappings exist), and the address
 * space is not the active CR3.
 */
void paging_user_destroy(struct addrspace *as);

/*
 * Copy every user (lower-half) 4 KiB mapping of `src` into `dst`:
 * fresh frames, page contents duplicated, leaf permissions preserved
 * (fork's eager copy; COW comes later). `dst` must have no user
 * mappings yet. Returns PAGING_OK or PAGING_ENOMEM — on failure the
 * partial copy stays in `dst` for paging_user_destroy() to reclaim.
 */
int paging_user_clone(struct addrspace *dst, const struct addrspace *src);
