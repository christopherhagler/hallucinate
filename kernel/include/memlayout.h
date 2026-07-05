/*
 * memlayout.h - kernel virtual memory layout.
 *
 * The boot page tables (built by stage 2) map the first 1 GiB of physical
 * memory twice: identity at 0 and at KERNEL_VMA. The kernel lives in the
 * higher half; phys_to_virt is valid only for physical addresses below
 * BOOT_MAPPED_LIMIT until the real VMM (Phase 2) takes over.
 */
#ifndef HL_MEMLAYOUT_H
#define HL_MEMLAYOUT_H

#include <stdint.h>

#define KERNEL_VMA        0xffffffff80000000ull
#define BOOT_MAPPED_LIMIT 0x40000000ull /* 1 GiB */

static inline void *phys_to_virt(uint64_t pa) {
    return (void *)(uintptr_t)(pa + KERNEL_VMA);
}

static inline uint64_t virt_to_phys(const void *va) {
    return (uint64_t)(uintptr_t)va - KERNEL_VMA;
}

#endif /* HL_MEMLAYOUT_H */
