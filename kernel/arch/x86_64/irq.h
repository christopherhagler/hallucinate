/*
 * irq.h - hardware interrupt (IRQ line) dispatch.
 *
 * Sits between the trap layer and device drivers: owns vectors
 * VEC_IRQ_BASE..+15, filters spurious IRQs, and sends the EOI after
 * the handler runs, so drivers deal only in IRQ line numbers.
 */
#pragma once

#include <stdint.h>

typedef void (*irq_handler_t)(void);

/* Claim vectors 32-47 from the trap layer. Call after idt/pic init. */
void irq_init(void);

/*
 * Install a handler for an IRQ line (0-15) and unmask it. A line may
 * have exactly one handler; re-registering is a bug (panic).
 */
void irq_register(uint8_t irq, irq_handler_t handler);
