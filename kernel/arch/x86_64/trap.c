/*
 * trap.c - IDT setup and central trap dispatch.
 */
#include <arch/x86_64/trap.h>

#include <stddef.h>
#include <stdint.h>

#include <arch/x86_64/cpu.h>
#include <arch/x86_64/gdt.h>
#include <compiler.h>
#include <kprintf.h>
#include <panic.h>

struct idt_gate {
    uint16_t offset_lo;
    uint16_t selector;
    uint8_t ist;       /* bits 0-2: IST index, 0 = none */
    uint8_t type_attr; /* P | DPL | type */
    uint16_t offset_mid;
    uint32_t offset_hi;
    uint32_t reserved;
} PACKED;

struct idtr {
    uint16_t limit;
    uint64_t base;
} PACKED;

#define GATE_INTERRUPT 0x8E /* present, DPL 0, 64-bit interrupt gate */

extern const uint64_t isr_stub_table[256]; /* isr.asm */

static struct idt_gate idt[256];
static trap_handler_t handlers[256];

static const char *const exception_names[32] = {
    "#DE divide error",
    "#DB debug",
    "NMI",
    "#BP breakpoint",
    "#OF overflow",
    "#BR bound range",
    "#UD invalid opcode",
    "#NM device not available",
    "#DF double fault",
    "coprocessor segment overrun",
    "#TS invalid TSS",
    "#NP segment not present",
    "#SS stack fault",
    "#GP general protection",
    "#PF page fault",
    "reserved (15)",
    "#MF x87 FP error",
    "#AC alignment check",
    "#MC machine check",
    "#XM SIMD FP error",
    "#VE virtualization",
    "#CP control protection",
    "reserved (22)",
    "reserved (23)",
    "reserved (24)",
    "reserved (25)",
    "reserved (26)",
    "reserved (27)",
    "#HV hypervisor injection",
    "#VC VMM communication",
    "#SX security",
    "reserved (31)",
};

static void dump_trapframe(const struct trapframe *tf) {
    kprintf("trap: vector %llu, error code %#llx\n", (unsigned long long)tf->vector,
            (unsigned long long)tf->error);
    kprintf("  rip=%016llx cs=%04llx rflags=%08llx\n", (unsigned long long)tf->rip,
            (unsigned long long)tf->cs, (unsigned long long)tf->rflags);
    kprintf("  rsp=%016llx ss=%04llx\n", (unsigned long long)tf->rsp, (unsigned long long)tf->ss);
    kprintf("  rax=%016llx rbx=%016llx rcx=%016llx\n", (unsigned long long)tf->rax,
            (unsigned long long)tf->rbx, (unsigned long long)tf->rcx);
    kprintf("  rdx=%016llx rsi=%016llx rdi=%016llx\n", (unsigned long long)tf->rdx,
            (unsigned long long)tf->rsi, (unsigned long long)tf->rdi);
    kprintf("  rbp=%016llx r8 =%016llx r9 =%016llx\n", (unsigned long long)tf->rbp,
            (unsigned long long)tf->r8, (unsigned long long)tf->r9);
    kprintf("  r10=%016llx r11=%016llx r12=%016llx\n", (unsigned long long)tf->r10,
            (unsigned long long)tf->r11, (unsigned long long)tf->r12);
    kprintf("  r13=%016llx r14=%016llx r15=%016llx\n", (unsigned long long)tf->r13,
            (unsigned long long)tf->r14, (unsigned long long)tf->r15);
    if (tf->vector == VEC_PAGE_FAULT) {
        kprintf("  cr2=%016llx cr3=%016llx\n", (unsigned long long)read_cr2(),
                (unsigned long long)read_cr3());
    }
}

trap_handler_t trap_register(uint8_t vector, trap_handler_t handler) {
    trap_handler_t prev = handlers[vector];
    handlers[vector] = handler;
    return prev;
}

/* Called from isr.asm for every interrupt and exception. */
void trap_dispatch(struct trapframe *tf) {
    trap_handler_t h = handlers[tf->vector];
    if (h != NULL) {
        h(tf);
        return;
    }
    if (tf->vector < 32) {
        dump_trapframe(tf);
        panic("unhandled exception %llu (%s)", (unsigned long long)tf->vector,
              exception_names[tf->vector]);
    }
    /* No stray interrupts can occur while every line is masked; one
     * firing anyway means broken interrupt routing. */
    dump_trapframe(tf);
    panic("unexpected interrupt, vector %llu", (unsigned long long)tf->vector);
}

static void set_gate(int vector, uint64_t stub, uint8_t ist) {
    idt[vector] = (struct idt_gate){
        .offset_lo = (uint16_t)stub,
        .selector = SEL_KCODE,
        .ist = ist,
        .type_attr = GATE_INTERRUPT,
        .offset_mid = (uint16_t)(stub >> 16),
        .offset_hi = (uint32_t)(stub >> 32),
        .reserved = 0,
    };
}

void idt_init(void) {
    for (int v = 0; v < 256; v++) {
        set_gate(v, isr_stub_table[v], 0);
    }
    /* A double fault must run on a known-good stack. */
    set_gate(VEC_DOUBLE_FAULT, isr_stub_table[VEC_DOUBLE_FAULT], IST_DOUBLE_FAULT);

    struct idtr idtr = {
        .limit = sizeof(idt) - 1,
        .base = (uint64_t)idt,
    };
    __asm__ volatile("lidt %0" : : "m"(idtr));
}
