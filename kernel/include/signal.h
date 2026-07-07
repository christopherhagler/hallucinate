/*
 * signal.h - signal numbers (Linux x86_64 values).
 *
 * There is no signal delivery machinery yet; these numbers are the
 * wait-status vocabulary for processes killed by hardware faults
 * (see process_kill). Full signals arrive with the Linux
 * compatibility work.
 */
#pragma once

#define SIGILL  4
#define SIGTRAP 5
#define SIGBUS  7
#define SIGFPE  8
#define SIGSEGV 11
