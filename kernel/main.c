/*
 * main.c - kernel C entry point.
 *
 * Entered from arch/x86_64/entry.asm with the physical address of the
 * bootinfo block (see docs/boot-protocol.md) as the only argument.
 */
#include <stdint.h>

#include <arch/x86_64/cpu.h>
#include <arch/x86_64/gdt.h>
#include <arch/x86_64/irq.h>
#include <arch/x86_64/pic.h>
#include <arch/x86_64/trap.h>
#include <bootinfo.h>
#include <compiler.h>
#include <console.h>
#include <init.h>
#include <keyboard.h>
#include <kmalloc.h>
#include <kprintf.h>
#include <memlayout.h>
#include <panic.h>
#include <pmm.h>
#include <selftest.h>
#include <timer.h>
#include <vmm.h>

#define KERNEL_VERSION "0.2.0"
#define TIMER_HZ       100

static const char *e820_type_name(uint32_t type) {
    switch (type) {
    case E820_TYPE_USABLE:
        return "usable";
    case E820_TYPE_RESERVED:
        return "reserved";
    case E820_TYPE_ACPI_RECLAIM:
        return "ACPI reclaimable";
    case E820_TYPE_ACPI_NVS:
        return "ACPI NVS";
    case E820_TYPE_BAD:
        return "bad RAM";
    default:
        return "unknown";
    }
}

static const struct bootinfo *bootinfo_get(uint64_t phys) {
    if (phys == 0 || phys >= BOOT_MAPPED_LIMIT) {
        panic("bootinfo pointer %#llx outside boot-mapped memory", (unsigned long long)phys);
    }
    const struct bootinfo *bi = phys_to_virt(phys);
    if (bi->magic != BOOTINFO_MAGIC) {
        panic("bootinfo magic %#x, expected %#x", bi->magic, BOOTINFO_MAGIC);
    }
    if (bi->version != BOOTINFO_VERSION) {
        panic("boot protocol v%u, kernel speaks v%u", bi->version, BOOTINFO_VERSION);
    }
    if (bi->e820_count == 0 || bi->e820_count > E820_MAX_ENTRIES) {
        panic("bootinfo has %u e820 entries", bi->e820_count);
    }
    return bi;
}

static uint64_t print_memory_map(const struct bootinfo *bi) {
    uint64_t usable = 0;
    kprintf("e820: %u entries\n", bi->e820_count);
    for (uint32_t i = 0; i < bi->e820_count; i++) {
        const struct e820_entry *e = &bi->e820[i];
        kprintf("e820:  [0x%010llx - 0x%010llx] %s\n", (unsigned long long)e->base,
                (unsigned long long)(e->base + e->len - 1), e820_type_name(e->type));
        if (e->type == E820_TYPE_USABLE) {
            usable += e->len;
        }
    }
    return usable;
}

void kmain(uint64_t bootinfo_phys) {
    console_init();
    kprintf("\nHallucinate OS v" KERNEL_VERSION " (x86_64)\n");

    gdt_init();
    idt_init();
    pic_init();
    irq_init();
    kprintf("cpu: GDT/TSS loaded, IDT ready (256 vectors), PIC remapped and masked\n");

    const struct bootinfo *bi = bootinfo_get(bootinfo_phys);
    kprintf("boot: BIOS drive %#x, boot protocol v%u\n", bi->boot_drive, bi->version);

    uint64_t usable = print_memory_map(bi);
    kprintf("memory: %llu MiB usable\n", (unsigned long long)(usable >> 20));

    pmm_init(bi);
    vmm_init(bi);
    kmalloc_init();
    kprintf("heap: slab allocator ready\n");

    timer_init(TIMER_HZ);
    keyboard_init();
    cpu_enable_interrupts();

    uint64_t start = timer_ticks();
    timer_sleep_ticks(3);
    kprintf("timer: %u Hz, ticking (slept %llu ticks)\n", timer_hz(),
            (unsigned long long)(timer_ticks() - start));

    selftest_run();

    kprintf("boot: complete\n");

    /*
     * Interactive placeholder until the shell exists (Phase 6): echo
     * keyboard input to both consoles, halting between interrupts.
     */
    kprintf("keyboard: type in the QEMU window; input echoes here\n");
    for (;;) {
        int c = keyboard_getchar();
        if (c < 0) {
            cpu_wait_for_interrupt();
            continue;
        }
        kprintf("%c", c);
    }
}
