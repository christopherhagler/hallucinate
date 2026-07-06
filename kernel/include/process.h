/*
 * process.h - user processes.
 *
 * Phase 4, first slice: a single process ("init") whose program is a
 * flat binary embedded in the kernel image, hosted by one kernel
 * thread. The ELF loader, PIDs beyond init, and fork/exec/wait build
 * on this in the following slices.
 */
#pragma once

#include <compiler.h>

/* PID of init, the only process until the process table lands. */
#define INIT_PID 1

/*
 * Launch init from the embedded program image, wait for it to exit,
 * and tear its address space down. Requires the scheduler running
 * and syscall_init() done. Panics if init cannot be built.
 */
void process_run_init(void);

/* Terminate the calling process (from SYS_exit). */
NORETURN void process_exit(int status);
