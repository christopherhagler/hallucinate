/*
 * pit.c - Intel 8254 programmable interval timer, channel 0.
 *
 * Channel 0 is wired to IRQ 0 and programmed as a rate generator
 * (mode 2). The 1.193182 MHz input clock rarely divides evenly, so
 * the real tick rate is within one part in ~12000 of the request;
 * uptime is derived from ticks and the requested rate.
 */
#include <timer.h>

#include <arch/x86_64/cpu.h>
#include <arch/x86_64/io.h>
#include <arch/x86_64/irq.h>
#include <panic.h>

#define PIT_CH0 0x40
#define PIT_CMD 0x43
#define PIT_HZ  1193182u
/* Command: channel 0, lobyte/hibyte access, mode 2, binary. */
#define PIT_CMD_CH0_RATE 0x34

#define IRQ_TIMER 0

static volatile uint64_t ticks;
static uint32_t configured_hz;

static void timer_irq(void) {
    ticks++;
}

void timer_init(uint32_t hz) {
    KASSERT(hz >= 19 && hz <= PIT_HZ); /* divisor must fit in 16 bits */
    uint32_t divisor = (PIT_HZ + (hz / 2)) / hz;
    KASSERT(divisor >= 1 && divisor <= 65535);

    configured_hz = hz;
    outb(PIT_CMD, PIT_CMD_CH0_RATE);
    outb(PIT_CH0, (uint8_t)divisor);
    outb(PIT_CH0, (uint8_t)(divisor >> 8));

    irq_register(IRQ_TIMER, timer_irq);
}

uint32_t timer_hz(void) {
    return configured_hz;
}

uint64_t timer_ticks(void) {
    /* Aligned 8-byte load; atomic on x86_64, and the writer is an
     * interrupt on this same CPU. */
    return ticks;
}

uint64_t timer_uptime_ms(void) {
    return timer_ticks() * 1000u / configured_hz;
}

void timer_sleep_ticks(uint64_t n) {
    if (!cpu_interrupts_enabled()) {
        panic("timer_sleep_ticks with interrupts disabled");
    }
    uint64_t until = timer_ticks() + n;
    while (timer_ticks() < until) {
        cpu_wait_for_interrupt();
    }
}
