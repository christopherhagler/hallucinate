/*
 * timer.h - system tick timer.
 *
 * Backed by the 8254 PIT until the APIC timer arrives; the interface
 * is arch-neutral on purpose.
 */
#pragma once

#include <stdint.h>

/*
 * Program the tick source to the requested frequency, register its
 * interrupt handler, and unmask its line. Interrupts must still be
 * enabled by the caller before ticks advance.
 */
void timer_init(uint32_t hz);

/* Configured tick frequency in Hz. */
uint32_t timer_hz(void);

/* Monotonic tick count since timer_init(). */
uint64_t timer_ticks(void);

/* Milliseconds since timer_init(), derived from the tick count. */
uint64_t timer_uptime_ms(void);

/*
 * Block for at least the given number of ticks, halting between
 * interrupts. Requires interrupts enabled; panics otherwise (it could
 * never wake).
 */
void timer_sleep_ticks(uint64_t n);
