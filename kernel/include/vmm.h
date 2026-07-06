/*
 * vmm.h - kernel virtual memory manager.
 */
#pragma once

#include <stdint.h>

#include <bootinfo.h>

struct addrspace;

/*
 * Replace the boot page tables with kernel-owned ones:
 *
 *   - HHDM: all physical memory (and the legacy/PCI MMIO window up to
 *     4 GiB) direct-mapped at HHDM_BASE with 2 MiB global pages, NX,
 *     RAM write-back and non-RAM cache-disabled.
 *   - Kernel image at KERNEL_VMA with 4 KiB pages and W^X: .text RX,
 *     .rodata RO+NX, .data/.bss RW+NX.
 *   - No identity map: the null page and all of userspace-to-be are
 *     unmapped.
 *
 * Also enables EFER.NXE, CR0.WP, and CR4.PGE, flips hhdm_base to the
 * new direct map, and installs a decoding page-fault handler.
 * Requires pmm_init() first.
 */
void vmm_init(const struct bootinfo *bi);

/* Leaf PTE covering a kernel virtual address (0 if unmapped);
 * translated physical address in *phys_out when non-NULL. */
uint64_t vmm_kernel_lookup(uint64_t virt, uint64_t *phys_out);

/* The kernel address space (what CR3 holds when no process runs). */
struct addrspace *vmm_kernel_addrspace(void);

/*
 * Create a user address space: fresh lower half, kernel upper half
 * shared by aliasing the kernel PML4's entries 256-511 (kernel
 * mappings lack PTE_US, so ring 3 still cannot touch them).
 * Returns 0 or -ENOMEM.
 */
int vmm_addrspace_create_user(struct addrspace *as);
