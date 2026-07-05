/*
 * irq.c - hardware interrupt dispatch over the PIC.
 */
#include <arch/x86_64/irq.h>

#include <stddef.h>

#include <arch/x86_64/pic.h>
#include <arch/x86_64/trap.h>
#include <panic.h>

static irq_handler_t handlers[PIC_IRQS];
static uint64_t spurious_count;

static void irq_dispatch(struct trapframe *tf) {
    uint8_t irq = (uint8_t)(tf->vector - VEC_IRQ_BASE);

    if ((irq == 7 || irq == 15) && pic_is_spurious(irq)) {
        /* No EOI for the faked line (pic_is_spurious already handled
         * the master's share for IRQ 15). */
        spurious_count++;
        return;
    }

    irq_handler_t h = handlers[irq];
    if (h == NULL) {
        /* Only unmasked lines can fire, and irq_register() is the
         * only unmasker, so this indicates broken routing. */
        panic("interrupt on IRQ %u with no handler", irq);
    }
    h();
    pic_send_eoi(irq);
}

void irq_init(void) {
    for (uint8_t irq = 0; irq < PIC_IRQS; irq++) {
        trap_register((uint8_t)(VEC_IRQ_BASE + irq), irq_dispatch);
    }
}

void irq_register(uint8_t irq, irq_handler_t handler) {
    KASSERT(irq < PIC_IRQS);
    KASSERT(handler != NULL);
    if (handlers[irq] != NULL) {
        panic("IRQ %u already has a handler", irq);
    }
    handlers[irq] = handler;
    pic_unmask(irq);
}
