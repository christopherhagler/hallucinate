/*
 * trap.h - trap frames and trap handler registration.
 *
 * Every interrupt and exception funnels through the stubs in isr.asm
 * into trap_dispatch() with a struct trapframe describing the
 * interrupted context. Handlers are registered per vector; a vector
 * with no handler is fatal (exceptions dump state and panic, stray
 * interrupts panic outright — the PIC is fully masked until a driver
 * unmasks its line and registers a handler first).
 */
#pragma once

#include <stdint.h>

/* Layout matches the push sequence in isr.asm; do not reorder. */
struct trapframe {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t vector;
    uint64_t error; /* CPU error code, or 0 for vectors without one */
    /* Pushed by the CPU: */
    uint64_t rip, cs, rflags, rsp, ss;
};

typedef void (*trap_handler_t)(struct trapframe *tf);

/* CPU exception vectors used by name. */
#define VEC_BREAKPOINT   3
#define VEC_DOUBLE_FAULT 8
#define VEC_GP_FAULT     13
#define VEC_PAGE_FAULT   14

/* First vector the legacy PIC IRQ lines are remapped to. */
#define VEC_IRQ_BASE 32

/*
 * Install a handler for a vector, returning the previous one (NULL if
 * none). Pass NULL to restore fatal default behavior.
 */
trap_handler_t trap_register(uint8_t vector, trap_handler_t handler);

/* Build and load the IDT. Requires gdt_init() first (IST for #DF). */
void idt_init(void);

/* Entry from isr.asm only; never call directly. */
void trap_dispatch(struct trapframe *tf);
