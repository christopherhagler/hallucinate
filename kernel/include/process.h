/*
 * process.h - user processes.
 *
 * Phase 4, current slice: a single process ("init") loaded from an
 * ELF64 image embedded in the kernel, hosted by one kernel thread.
 * PIDs beyond init and fork/exec/wait build on this in the next
 * slice.
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
