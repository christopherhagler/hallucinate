/*
 * ctx.h - kernel thread context switch (implemented in ctx.asm).
 */
#pragma once

#include <stdint.h>

/*
 * Save the callee-saved context of the calling thread on its stack,
 * store the resulting stack pointer to *save_rsp, then adopt
 * load_rsp and return as the incoming thread. Interrupts must be
 * disabled across the call.
 */
void ctx_switch(uint64_t *save_rsp, uint64_t load_rsp);

/*
 * Entry point placed as the return address in a new thread's initial
 * stack frame; expects r12 = entry function, r13 = argument. Enables
 * interrupts, runs entry(argument), then falls into thread_exit().
 * Never called from C — its address is taken by sched.c only.
 */
void thread_entry_trampoline(void);
