/*
 * syscall.h - the native system call interface.
 *
 * ABI (identical to Linux x86_64 by design — see docs/book/appendix-h-userspace.md):
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

#define SYS_read       0
#define SYS_write      1
#define SYS_open       2
#define SYS_close      3
#define SYS_fstat      5
#define SYS_lseek      8
#define SYS_pipe       22
#define SYS_getpid     39
#define SYS_fork       57
#define SYS_execve     59
#define SYS_exit       60
#define SYS_wait4      61
#define SYS_fsync      74
#define SYS_rename     82
#define SYS_mkdir      83
#define SYS_rmdir      84
#define SYS_link       86
#define SYS_unlink     87
#define SYS_getdents64 217

/*
 * The complete user register state at syscall entry, built on the
 * kernel stack by syscall_entry.asm (field order mirrors the push
 * sequence — asserted with offsetof in syscall.c). The entry stub
 * restores every field on the way out, which is what makes the ABI's
 * "only rax/rcx/r11 change" promise true, and fork() duplicates the
 * whole struct to give the child an identical user context.
 */
struct syscall_frame {
    uint64_t r15, r14, r13, r12, rbp, rbx; /* callee-saved */
    uint64_t r9, r8, r10, rdx, rsi, rdi;   /* argument registers */
    uint64_t rax;                          /* nr in, return value out */
    uint64_t rflags;                       /* from r11 */
    uint64_t rip;                          /* from rcx */
    uint64_t rsp;                          /* user stack */
};

/* Program the SYSCALL/SYSRET MSRs (EFER.SCE, STAR, LSTAR, SFMASK).
 * Requires gdt_init(); the selector layout in gdt.h is built for it. */
void syscall_init(void);

/* Called by trap-free context switches: the kernel stack the syscall
 * entry adopts for the incoming thread. */
void syscall_set_kstack(uint64_t kstack_top);

/* C-level dispatcher, called from the entry stub with the frame on
 * the current kernel stack; writes the result into frame->rax.
 * Never call directly. */
void syscall_dispatch(struct syscall_frame *frame);
