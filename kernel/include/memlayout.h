/*
 * memlayout.h - kernel virtual memory layout.
 *
 * Two fixed landmarks in the higher half:
 *
 *   HHDM_BASE   direct map of all physical memory ("higher-half direct
 *               map"), installed by vmm_init(). Every physical byte is
 *               reachable at HHDM_BASE + paddr.
 *   KERNEL_VMA  the kernel image mapping (top 2 GiB, required by
 *               -mcmodel=kernel). Only the image itself lives here.
 *
 * Before vmm_init(), the boot page tables (stage 2) map only the first
 * 1 GiB of physical memory at KERNEL_VMA, so hhdm_base starts there and
 * phys_to_virt() is valid only below BOOT_MAPPED_LIMIT. After
 * vmm_init() flips hhdm_base to HHDM_BASE, phys_to_virt() covers all
 * of physical memory.
 */
#ifndef HL_MEMLAYOUT_H
#define HL_MEMLAYOUT_H

#include <stdint.h>

#define KERNEL_VMA        0xffffffff80000000ull
#define HHDM_BASE         0xffff800000000000ull
#define BOOT_MAPPED_LIMIT 0x40000000ull /* 1 GiB */

/* The direct map always covers at least the first 4 GiB (RAM plus the
 * legacy/MMIO window), so device BARs below this are reachable via
 * phys_to_virt(). Established by vmm_init(). */
#define MMIO_LIMIT 0x100000000ull

/* End of user virtual addresses (the canonical lower half); user
 * mappings and user-supplied pointers must sit strictly below it. */
#define USER_VA_LIMIT 0x0000800000000000ull

/* Current direct-map base; owned by kernel/mm/vmm.c. */
extern uint64_t hhdm_base;

static inline void *phys_to_virt(uint64_t pa) {
    return (void *)(uintptr_t)(pa + hhdm_base);
}

/* Valid only for pointers derived from phys_to_virt(), NOT for
 * kernel-image addresses (those live at KERNEL_VMA, not the HHDM). */
static inline uint64_t virt_to_phys(const void *va) {
    return (uint64_t)(uintptr_t)va - hhdm_base;
}

#endif /* HL_MEMLAYOUT_H */
