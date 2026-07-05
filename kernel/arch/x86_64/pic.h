/*
 * pic.h - legacy 8259A programmable interrupt controller.
 *
 * The two cascaded PICs are remapped so IRQ 0-15 land on vectors
 * 0x20-0x2F (VEC_IRQ_BASE), clear of the CPU exception range. Every
 * line starts masked; drivers unmask their line only after
 * registering a trap handler for it.
 */
#pragma once

#include <stdint.h>

#define PIC_IRQS 16

/* Remap both PICs to VEC_IRQ_BASE and mask every line. */
void pic_init(void);

/* Unmask/mask a single IRQ line (0-15). */
void pic_unmask(uint8_t irq);
void pic_mask(uint8_t irq);

/* Signal end-of-interrupt for an IRQ line (0-15). */
void pic_send_eoi(uint8_t irq);

/*
 * True if this IRQ is a spurious IRQ 7/15 (no ISR bit set). The
 * caller must not send an EOI for a spurious IRQ 7; for a spurious
 * IRQ 15 only the master gets an EOI, which this function performs.
 */
int pic_is_spurious(uint8_t irq);
