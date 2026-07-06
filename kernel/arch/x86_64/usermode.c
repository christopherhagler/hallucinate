/*
 * usermode.c - SYSCALL/SYSRET machine setup.
 *
 * The GDT selector layout (gdt.h) exists to make these two STAR
 * fields legal:
 *   STAR[47:32] = kernel code; SYSCALL loads CS = it, SS = it + 8.
 *   STAR[63:48] = user base;   SYSRET  loads CS = it + 16 (64-bit),
 *                              SS = it + 8.
 */
#include <syscall.h>

#include <arch/x86_64/cpu.h>
#include <arch/x86_64/gdt.h>

#define MSR_EFER   0xC0000080u
#define MSR_STAR   0xC0000081u
#define MSR_LSTAR  0xC0000082u
#define MSR_SFMASK 0xC0000084u

#define EFER_SCE (1ull << 0)

/* RFLAGS cleared on syscall entry: interrupts stay off until the
 * entry stub stands on a kernel stack; TF/DF/AC must never leak from
 * user into kernel execution. */
#define SFMASK_BITS 0x40700ull /* TF | IF | DF | AC */

void syscall_entry(void); /* syscall_entry.asm */
extern uint64_t syscall_kstack;

void syscall_init(void) {
    uint64_t star = ((uint64_t)(SEL_UCODE32 | 3) << 48) | ((uint64_t)SEL_KCODE << 32);
    wrmsr(MSR_STAR, star);
    wrmsr(MSR_LSTAR, (uint64_t)syscall_entry);
    wrmsr(MSR_SFMASK, SFMASK_BITS);
    wrmsr(MSR_EFER, rdmsr(MSR_EFER) | EFER_SCE);
}

void syscall_set_kstack(uint64_t kstack_top) {
    syscall_kstack = kstack_top;
}
