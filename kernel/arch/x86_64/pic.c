/*
 * pic.c - legacy 8259A PIC driver.
 *
 * Reference: Intel 8259A datasheet. The APIC replaces this in a later
 * phase; the driver interface (mask/unmask/EOI per line) stays.
 */
#include <arch/x86_64/pic.h>

#include <arch/x86_64/io.h>
#include <arch/x86_64/trap.h>
#include <panic.h>

#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1

#define ICW1_INIT 0x11 /* edge-triggered, cascade, ICW4 needed */
#define ICW4_8086 0x01
#define OCW2_EOI  0x20
#define OCW3_ISR  0x0B /* next read on the command port returns ISR */

void pic_init(void) {
    /* ICW1: start initialization on both PICs. */
    outb(PIC1_CMD, ICW1_INIT);
    io_wait();
    outb(PIC2_CMD, ICW1_INIT);
    io_wait();
    /* ICW2: vector offsets. */
    outb(PIC1_DATA, VEC_IRQ_BASE);
    io_wait();
    outb(PIC2_DATA, VEC_IRQ_BASE + 8);
    io_wait();
    /* ICW3: master has the slave on line 2; slave identity is 2. */
    outb(PIC1_DATA, 1 << 2);
    io_wait();
    outb(PIC2_DATA, 2);
    io_wait();
    /* ICW4: 8086 mode. */
    outb(PIC1_DATA, ICW4_8086);
    io_wait();
    outb(PIC2_DATA, ICW4_8086);
    io_wait();
    /* Mask everything; drivers unmask their own lines. */
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);
}

void pic_unmask(uint8_t irq) {
    KASSERT(irq < PIC_IRQS);
    if (irq < 8) {
        outb(PIC1_DATA, inb(PIC1_DATA) & (uint8_t)~(1u << irq));
    } else {
        outb(PIC2_DATA, inb(PIC2_DATA) & (uint8_t)~(1u << (irq - 8)));
        /* The slave is wired through master line 2. */
        outb(PIC1_DATA, inb(PIC1_DATA) & (uint8_t)~(1u << 2));
    }
}

void pic_mask(uint8_t irq) {
    KASSERT(irq < PIC_IRQS);
    if (irq < 8) {
        outb(PIC1_DATA, inb(PIC1_DATA) | (uint8_t)(1u << irq));
    } else {
        outb(PIC2_DATA, inb(PIC2_DATA) | (uint8_t)(1u << (irq - 8)));
    }
}

void pic_send_eoi(uint8_t irq) {
    KASSERT(irq < PIC_IRQS);
    if (irq >= 8) {
        outb(PIC2_CMD, OCW2_EOI);
    }
    outb(PIC1_CMD, OCW2_EOI);
}

int pic_is_spurious(uint8_t irq) {
    if (irq == 7) {
        outb(PIC1_CMD, OCW3_ISR);
        return (inb(PIC1_CMD) & 0x80) == 0;
    }
    if (irq == 15) {
        outb(PIC2_CMD, OCW3_ISR);
        if ((inb(PIC2_CMD) & 0x80) == 0) {
            /* The master saw line 2 assert regardless; acknowledge it. */
            outb(PIC1_CMD, OCW2_EOI);
            return 1;
        }
    }
    return 0;
}
