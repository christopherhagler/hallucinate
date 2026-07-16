/*
 * keyboard.h - PS/2 keyboard input.
 */
#pragma once

/* Register the keyboard interrupt handler and unmask its IRQ line. */
void keyboard_init(void);

/*
 * Pop one translated character from the input buffer.
 * Returns the character (> 0), or -1 if the buffer is empty.
 * Safe to call with interrupts enabled; never blocks.
 */
int keyboard_getchar(void);

/*
 * Register a callback invoked from the keyboard IRQ handler
 * (interrupts off) after new input lands in the buffer. One
 * consumer: the console device uses it to wake a blocked reader.
 */
void keyboard_set_notify(void (*fn)(void));
