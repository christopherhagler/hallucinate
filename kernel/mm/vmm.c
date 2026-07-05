/*
 * vmm.c - kernel address space construction.
 *
 * See vmm.h for the layout contract. Construction runs on the boot
 * page tables (first GiB aliased at KERNEL_VMA), which is why the PMM
 * keeps its early allocations below BOOT_MAPPED_LIMIT; the switch to
 * the new tables and the hhdm_base flip happen back to back at the
 * end, after which the old alias is gone.
 */
#include <vmm.h>

#include <stddef.h>

#include <arch/x86_64/cpu.h>
#include <arch/x86_64/paging.h>
#include <arch/x86_64/trap.h>
#include <kprintf.h>
#include <memlayout.h>
#include <panic.h>
#include <pmm.h>

/* Linker-script section bounds (virtual). */
extern char _kernel_start[];
extern char _text_start[];
extern char _text_end[];
extern char _rodata_start[];
extern char _rodata_end[];
extern char _data_start[];
extern char _data_end[];
extern char _kernel_end[];

/* Boot alias until vmm_init() installs the real direct map. */
uint64_t hhdm_base = KERNEL_VMA;

#define SIZE_2M    0x200000ull
#define MMIO_LIMIT 0x100000000ull /* always direct-map the first 4 GiB */

#define EFER_MSR 0xC0000080u
#define EFER_NXE (1ull << 11)
#define CR0_WP   (1ull << 16)
#define CR4_PGE  (1ull << 7)

static struct addrspace kernel_as;

static uint64_t align_up(uint64_t v, uint64_t a) {
    return (v + a - 1) & ~(a - 1);
}

/* Does [base, base+SIZE_2M) intersect any E820-usable RAM? */
static int chunk_is_ram(const struct bootinfo *bi, uint64_t base) {
    for (uint32_t i = 0; i < bi->e820_count; i++) {
        const struct e820_entry *e = &bi->e820[i];
        if (e->type == E820_TYPE_USABLE && e->base < base + SIZE_2M && base < e->base + e->len) {
            return 1;
        }
    }
    return 0;
}

static void must_map_2m(uint64_t virt, uint64_t phys, uint64_t flags) {
    int rc = paging_map_2m(&kernel_as, virt, phys, flags);
    if (rc != PAGING_OK) {
        panic("vmm: mapping 2M page at %#llx failed (%d)", (unsigned long long)virt, rc);
    }
}

static void map_kernel_section(const char *start, const char *end, uint64_t flags) {
    uint64_t virt = (uint64_t)start;
    uint64_t virt_end = align_up((uint64_t)end, PAGE_SIZE);
    KASSERT((virt & (PAGE_SIZE - 1)) == 0);
    for (; virt < virt_end; virt += PAGE_SIZE) {
        int rc = paging_map_4k(&kernel_as, virt, virt - KERNEL_VMA, flags);
        if (rc != PAGING_OK) {
            panic("vmm: mapping kernel page at %#llx failed (%d)", (unsigned long long)virt, rc);
        }
    }
}

static void page_fault_handler(struct trapframe *tf) {
    uint64_t addr = read_cr2();
    trap_dump(tf);
    panic("page fault: %s %s at %#llx from rip %#llx%s%s",
          (tf->error & 2) ? "write" : ((tf->error & 16) ? "execute" : "read"),
          (tf->error & 1) ? "protection violation" : "of unmapped address",
          (unsigned long long)addr, (unsigned long long)tf->rip,
          (tf->error & 4) ? " (user mode)" : "", (tf->error & 8) ? " [reserved bit set!]" : "");
}

void vmm_init(const struct bootinfo *bi) {
    /* NX must be armed before any table with NX bits is live. */
    wrmsr(EFER_MSR, rdmsr(EFER_MSR) | EFER_NXE);

    if (paging_addrspace_create(&kernel_as) != PAGING_OK) {
        panic("vmm: out of frames for the kernel PML4");
    }

    /* Direct map: all usable RAM plus the 4 GiB legacy/MMIO window.
     * Reserved E820 ranges above 4 GiB (e.g. the 64-bit PCI hole) do
     * not extend the map; nothing addresses them yet. */
    uint64_t top = MMIO_LIMIT;
    for (uint32_t i = 0; i < bi->e820_count; i++) {
        const struct e820_entry *e = &bi->e820[i];
        if (e->type != E820_TYPE_USABLE) {
            continue;
        }
        uint64_t end = align_up(e->base + e->len, SIZE_2M);
        if (end > top) {
            top = end;
        }
    }
    uint64_t mapped_mib = 0;
    for (uint64_t pa = 0; pa < top; pa += SIZE_2M) {
        uint64_t flags = PTE_W | PTE_G | PTE_NX;
        if (!chunk_is_ram(bi, pa)) {
            flags |= PTE_PCD; /* device memory until finer PAT control */
        }
        must_map_2m(HHDM_BASE + pa, pa, flags);
        mapped_mib += 2;
    }

    /* Kernel image, W^X. */
    map_kernel_section(_text_start, _text_end, PTE_G);
    map_kernel_section(_rodata_start, _rodata_end, PTE_G | PTE_NX);
    map_kernel_section(_data_start, _data_end, PTE_G | PTE_W | PTE_NX);

    trap_register(VEC_PAGE_FAULT, page_fault_handler);

    /* Point of no return: new tables + new direct-map base together.
     * Everything holding a pointer derived from the old base must
     * re-derive it afterwards (currently only the PMM bitmap). */
    write_cr0(read_cr0() | CR0_WP);
    hhdm_base = HHDM_BASE;
    paging_activate(&kernel_as);
    write_cr4(read_cr4() | CR4_PGE);
    pmm_rebase();

    kprintf("vmm: kernel page tables active, %llu MiB direct-mapped at %#llx\n",
            (unsigned long long)mapped_mib, (unsigned long long)HHDM_BASE);
}

uint64_t vmm_kernel_lookup(uint64_t virt, uint64_t *phys_out) {
    return paging_lookup(&kernel_as, virt, phys_out);
}
