/*
 * cpu.h - x86_64 CPU control primitives.
 */
#ifndef HL_ARCH_X86_64_CPU_H
#define HL_ARCH_X86_64_CPU_H

#include <stdint.h>

#include <compiler.h>

static inline void cpu_relax(void) {
    __asm__ volatile("pause");
}

/* Enable/disable maskable interrupts on this CPU. */
static inline void cpu_enable_interrupts(void) {
    __asm__ volatile("sti");
}

static inline void cpu_disable_interrupts(void) {
    __asm__ volatile("cli");
}

/* Wait for the next interrupt with interrupts enabled during the halt. */
static inline void cpu_wait_for_interrupt(void) {
    __asm__ volatile("sti; hlt");
}

static inline uint64_t read_cr2(void) {
    uint64_t v;
    __asm__ volatile("mov %%cr2, %0" : "=r"(v));
    return v;
}

static inline uint64_t read_cr3(void) {
    uint64_t v;
    __asm__ volatile("mov %%cr3, %0" : "=r"(v));
    return v;
}

/* Stop this CPU permanently: interrupts off, halt loop. */
static inline NORETURN void cpu_halt_forever(void) {
    for (;;) {
        __asm__ volatile("cli; hlt");
    }
}

#endif /* HL_ARCH_X86_64_CPU_H */
