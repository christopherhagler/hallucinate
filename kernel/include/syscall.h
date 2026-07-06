/*
 * syscall.h - the native system call interface.
 *
 * ABI (identical to Linux x86_64 by design — see docs/userspace.md):
 *   entry        `syscall` instruction
 *   number       rax
 *   arguments    rdi, rsi, rdx, r10, r8, r9
 *   return       rax; errors are -errno (errno.h values)
 *   clobbered    rcx, r11 (by the hardware)
 *
 * Numbers match the Linux x86_64 table so the Phase 7 personality
 * layer needs no renumbering. Everything not implemented returns
 * -ENOSYS.
 */
#pragma once

#include <stdint.h>

#define SYS_write  1
#define SYS_getpid 39
#define SYS_exit   60

/* Program the SYSCALL/SYSRET MSRs (EFER.SCE, STAR, LSTAR, SFMASK).
 * Requires gdt_init(); the selector layout in gdt.h is built for it. */
void syscall_init(void);

/* Called by trap-free context switches: the kernel stack the syscall
 * entry adopts for the incoming thread. */
void syscall_set_kstack(uint64_t kstack_top);

/* C-level dispatcher, called from the entry stub with the syscall
 * number and its first four arguments. Never call directly. */
uint64_t syscall_dispatch(uint64_t nr, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4);
