/*
 * io.h - x86 port I/O primitives.
 */
#ifndef HL_ARCH_X86_64_IO_H
#define HL_ARCH_X86_64_IO_H

#include <stdint.h>

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t val;
    __asm__ volatile("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static inline void outw(uint16_t port, uint16_t val) {
    __asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint16_t inw(uint16_t port) {
    uint16_t val;
    __asm__ volatile("inw %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

/* Small delay for slow devices: a write to the POST diagnostic port. */
static inline void io_wait(void) {
    outb(0x80, 0);
}

#endif /* HL_ARCH_X86_64_IO_H */
