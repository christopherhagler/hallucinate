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

static inline int cpu_interrupts_enabled(void) {
    uint64_t rflags;
    __asm__ volatile("pushfq; popq %0" : "=r"(rflags));
    return (rflags & (1u << 9)) != 0; /* RFLAGS.IF */
}

/*
 * Nestable interrupt-off critical sections: capture RFLAGS and
 * disable, then restore the captured state (which re-enables only if
 * interrupts were on at entry).
 */
static inline uint64_t cpu_irq_save(void) {
    uint64_t rflags;
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(rflags) : : "memory");
    return rflags;
}

static inline void cpu_irq_restore(uint64_t rflags) {
    if (rflags & (1u << 9)) {
        __asm__ volatile("sti" : : : "memory");
    }
}

static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t lo;
    uint32_t hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

static inline void wrmsr(uint32_t msr, uint64_t value) {
    __asm__ volatile("wrmsr" : : "c"(msr), "a"((uint32_t)value), "d"((uint32_t)(value >> 32)));
}

static inline uint64_t read_cr0(void) {
    uint64_t v;
    __asm__ volatile("mov %%cr0, %0" : "=r"(v));
    return v;
}

static inline void write_cr0(uint64_t v) {
    __asm__ volatile("mov %0, %%cr0" : : "r"(v) : "memory");
}

static inline uint64_t read_cr4(void) {
    uint64_t v;
    __asm__ volatile("mov %%cr4, %0" : "=r"(v));
    return v;
}

static inline void write_cr4(uint64_t v) {
    __asm__ volatile("mov %0, %%cr4" : : "r"(v) : "memory");
}

static inline void write_cr3(uint64_t v) {
    __asm__ volatile("mov %0, %%cr3" : : "r"(v) : "memory");
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
