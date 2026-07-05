/*
 * cpu.h - x86_64 CPU control primitives.
 */
#ifndef HL_ARCH_X86_64_CPU_H
#define HL_ARCH_X86_64_CPU_H

#include <compiler.h>

static inline void cpu_relax(void) {
    __asm__ volatile("pause");
}

/* Stop this CPU permanently: interrupts off, halt loop. */
static inline NORETURN void cpu_halt_forever(void) {
    for (;;) {
        __asm__ volatile("cli; hlt");
    }
}

#endif /* HL_ARCH_X86_64_CPU_H */
