/*
 * gdt.h - kernel GDT, TSS, and segment selectors.
 *
 * Selector layout is fixed by the SYSCALL/SYSRET conventions used in
 * Phase 4: SYSCALL loads CS/SS from STAR[47:32] (+0/+8), SYSRET from
 * STAR[63:48] (+16/+8), which forces user data to sit between the
 * 32-bit and 64-bit user code slots.
 */
#pragma once

#include <stdint.h>

#define SEL_KCODE   0x08 /* kernel 64-bit code, DPL 0 */
#define SEL_KDATA   0x10 /* kernel data, DPL 0 */
#define SEL_UCODE32 0x18 /* reserved: keeps SYSRET selector math valid */
#define SEL_UDATA   0x20 /* user data, DPL 3 */
#define SEL_UCODE   0x28 /* user 64-bit code, DPL 3 */
#define SEL_TSS     0x30 /* 64-bit TSS (16-byte descriptor) */

/* IST slots (1-based, as encoded in IDT gates). */
#define IST_DOUBLE_FAULT 1

/* Install the kernel GDT and TSS, reloading all segment registers. */
void gdt_init(void);

/* Set the stack the CPU switches to on a ring 3 -> ring 0 transition. */
void gdt_set_rsp0(uint64_t rsp0);
