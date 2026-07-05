/*
 * gdt.c - kernel GDT and TSS.
 *
 * Replaces the minimal GDT stage 2 booted with. The TSS provides RSP0
 * for ring transitions (set per-thread once scheduling exists) and a
 * dedicated IST stack so a double fault always has a good stack, even
 * if the kernel stack itself is what went bad.
 */
#include <arch/x86_64/gdt.h>

#include <stdint.h>

#include <compiler.h>
#include <string.h>

struct tss64 {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist[7]; /* ist[0] is IST1 in IDT gate encoding */
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} PACKED;

struct gdtr {
    uint16_t limit;
    uint64_t base;
} PACKED;

/* Descriptor bits, 64-bit mode: P | DPL | S | type, plus L for code. */
#define DESC_KCODE 0x00209A0000000000ull /* P DPL0 S code RX, L=1 */
#define DESC_KDATA 0x0000920000000000ull /* P DPL0 S data RW */
#define DESC_UDATA 0x0000F20000000000ull /* P DPL3 S data RW */
#define DESC_UCODE 0x0020FA0000000000ull /* P DPL3 S code RX, L=1 */

static struct tss64 tss;
static uint64_t gdt[8];

/* 8 KiB emergency stack for double faults, via IST. */
static uint8_t ALIGNED(16) df_stack[8192];

static void tss_descriptor(uint64_t *lo, uint64_t *hi) {
    uint64_t base = (uint64_t)&tss;
    uint64_t limit = sizeof(tss) - 1;

    *lo = (limit & 0xFFFFull) | ((base & 0xFFFFFFull) << 16) |
          (0x89ull << 40) /* present, type 9: available 64-bit TSS */ |
          (((limit >> 16) & 0xFull) << 48) | (((base >> 24) & 0xFFull) << 56);
    *hi = base >> 32;
}

void gdt_set_rsp0(uint64_t rsp0) {
    tss.rsp0 = rsp0;
}

void gdt_init(void) {
    memset(&tss, 0, sizeof(tss));
    tss.ist[IST_DOUBLE_FAULT - 1] = (uint64_t)&df_stack[sizeof(df_stack)];
    tss.iomap_base = sizeof(tss); /* no I/O permission bitmap */

    gdt[0] = 0;
    gdt[SEL_KCODE >> 3] = DESC_KCODE;
    gdt[SEL_KDATA >> 3] = DESC_KDATA;
    gdt[SEL_UCODE32 >> 3] = 0; /* reserved slot, never loaded */
    gdt[SEL_UDATA >> 3] = DESC_UDATA;
    gdt[SEL_UCODE >> 3] = DESC_UCODE;
    tss_descriptor(&gdt[SEL_TSS >> 3], &gdt[(SEL_TSS >> 3) + 1]);

    struct gdtr gdtr = {
        .limit = sizeof(gdt) - 1,
        .base = (uint64_t)gdt,
    };
    __asm__ volatile("lgdt %0" : : "m"(gdtr));

    /* Reload CS with a far return, then the data segment registers. */
    __asm__ volatile("pushq %0\n\t"
                     "leaq 1f(%%rip), %%rax\n\t"
                     "pushq %%rax\n\t"
                     "lretq\n"
                     "1:\n\t"
                     "movl %1, %%eax\n\t"
                     "movl %%eax, %%ds\n\t"
                     "movl %%eax, %%es\n\t"
                     "movl %%eax, %%ss\n\t"
                     "xorl %%eax, %%eax\n\t"
                     "movl %%eax, %%fs\n\t"
                     "movl %%eax, %%gs\n\t"
                     :
                     : "i"((uint64_t)SEL_KCODE), "i"(SEL_KDATA)
                     : "rax", "memory");

    __asm__ volatile("ltr %w0" : : "r"((uint16_t)SEL_TSS));
}
