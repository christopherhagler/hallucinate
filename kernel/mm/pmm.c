/*
 * pmm.c - physical memory manager: E820 parsing, bitmap placement,
 * and the kernel-facing wrappers around pmm_core.
 *
 * Initial map construction (order matters, all steps clip/overlap
 * safely because the core marks are idempotent):
 *   1. every frame starts used
 *   2. free the page-aligned interior of each E820-usable region
 *   3. re-reserve every frame touched by a non-usable region, so
 *      overlapping or contradictory BIOS maps resolve conservatively
 *   4. reserve [0, end of kernel image) — low memory (IVT, bootinfo,
 *      boot page tables still in use) plus the kernel itself
 *   5. reserve the bitmap's own frames
 */
#include <pmm.h>

#include <kprintf.h>
#include <memlayout.h>
#include <panic.h>
#include <pmm_core.h>

/* Linker-provided kernel image bounds (virtual, higher half). */
extern char _kernel_start[];
extern char _kernel_end[];

static struct pmm pmm;
static uint64_t bitmap_phys_addr;

static uint64_t page_up(uint64_t addr) {
    return (addr + PAGE_SIZE - 1) & ~(uint64_t)(PAGE_SIZE - 1);
}

static uint64_t page_down(uint64_t addr) {
    return addr & ~(uint64_t)(PAGE_SIZE - 1);
}

/* Highest usable physical address the map can see. */
static uint64_t highest_usable_end(const struct bootinfo *bi) {
    uint64_t end = 0;
    for (uint32_t i = 0; i < bi->e820_count; i++) {
        const struct e820_entry *e = &bi->e820[i];
        if (e->type == E820_TYPE_USABLE && e->base + e->len > end) {
            end = e->base + e->len;
        }
    }
    return end;
}

/*
 * Find a home for the bitmap: the lowest page-aligned run of usable
 * memory of `size` bytes that starts at or above `min_addr` and stays
 * below BOOT_MAPPED_LIMIT (it must be addressable before the VMM
 * exists).
 */
static uint64_t place_bitmap(const struct bootinfo *bi, uint64_t size, uint64_t min_addr) {
    uint64_t best = 0;
    for (uint32_t i = 0; i < bi->e820_count; i++) {
        const struct e820_entry *e = &bi->e820[i];
        if (e->type != E820_TYPE_USABLE) {
            continue;
        }
        uint64_t start = page_up(e->base < min_addr ? min_addr : e->base);
        uint64_t end = page_down(e->base + e->len);
        if (end > BOOT_MAPPED_LIMIT) {
            end = BOOT_MAPPED_LIMIT;
        }
        if (start >= end || end - start < size) {
            continue;
        }
        if (best == 0 || start < best) {
            best = start;
        }
    }
    return best;
}

void pmm_init(const struct bootinfo *bi) {
    uint64_t kernel_end_phys = (uint64_t)_kernel_end - KERNEL_VMA;
    uint64_t mem_end = highest_usable_end(bi);
    if (mem_end == 0) {
        panic("pmm: E820 map has no usable memory");
    }

    uint64_t nframes = page_up(mem_end) >> PAGE_SHIFT;
    uint64_t bitmap_size = pmm_core_bitmap_size(nframes);
    uint64_t bitmap_phys = place_bitmap(bi, bitmap_size, kernel_end_phys);
    if (bitmap_phys == 0) {
        panic("pmm: no usable region for a %llu-byte frame bitmap",
              (unsigned long long)bitmap_size);
    }

    bitmap_phys_addr = bitmap_phys;
    pmm_core_init(&pmm, phys_to_virt(bitmap_phys), nframes);

    for (uint32_t i = 0; i < bi->e820_count; i++) {
        const struct e820_entry *e = &bi->e820[i];
        if (e->type != E820_TYPE_USABLE) {
            continue;
        }
        uint64_t start = page_up(e->base);
        uint64_t end = page_down(e->base + e->len);
        if (start < end) {
            pmm_core_mark_free(&pmm, start >> PAGE_SHIFT, (end - start) >> PAGE_SHIFT);
        }
    }
    for (uint32_t i = 0; i < bi->e820_count; i++) {
        const struct e820_entry *e = &bi->e820[i];
        if (e->type == E820_TYPE_USABLE) {
            continue;
        }
        uint64_t start = page_down(e->base);
        uint64_t end = page_up(e->base + e->len);
        pmm_core_mark_used(&pmm, start >> PAGE_SHIFT, (end - start) >> PAGE_SHIFT);
    }

    /* Low memory + kernel image, then the bitmap itself. */
    pmm_core_mark_used(&pmm, 0, page_up(kernel_end_phys) >> PAGE_SHIFT);
    pmm_core_mark_used(&pmm, bitmap_phys >> PAGE_SHIFT, page_up(bitmap_size) >> PAGE_SHIFT);

    kprintf("pmm: %llu MiB managed, %llu MiB free, bitmap %llu KiB at %#llx\n",
            (unsigned long long)(nframes >> 8), (unsigned long long)(pmm.free_frames >> 8),
            (unsigned long long)(bitmap_size >> 10), (unsigned long long)bitmap_phys);
}

uint64_t pmm_alloc_frame(void) {
    int64_t frame = pmm_core_alloc(&pmm);
    if (frame < 0) {
        return 0;
    }
    return (uint64_t)frame << PAGE_SHIFT;
}

uint64_t pmm_alloc_frames(uint64_t count, uint64_t align_frames) {
    int64_t frame = pmm_core_alloc_run(&pmm, count, align_frames);
    if (frame < 0) {
        return 0;
    }
    return (uint64_t)frame << PAGE_SHIFT;
}

void pmm_free_frame(uint64_t paddr) {
    if ((paddr & (PAGE_SIZE - 1)) != 0) {
        panic("pmm: freeing unaligned address %#llx", (unsigned long long)paddr);
    }
    if (pmm_core_free(&pmm, paddr >> PAGE_SHIFT) != 0) {
        panic("pmm: double or invalid free of frame %#llx", (unsigned long long)paddr);
    }
}

void pmm_rebase(void) {
    pmm.bitmap = phys_to_virt(bitmap_phys_addr);
}

uint64_t pmm_total_frames(void) {
    return pmm.nframes;
}

uint64_t pmm_free_frames(void) {
    return pmm.free_frames;
}
