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
 * never wake). Spins the calling context: use the scheduler's
 * sched_sleep_ticks() once threads exist.
 */
void timer_sleep_ticks(uint64_t n);

/*
 * Register a function called from the tick interrupt handler (so:
 * interrupt context, interrupts off, keep it short) with the new tick
 * count. One consumer — the scheduler; re-registration panics.
 */
typedef void (*timer_tick_fn)(uint64_t ticks);
void timer_set_tick_hook(timer_tick_fn fn);
