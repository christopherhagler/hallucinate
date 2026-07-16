/*
 * init.h - kernel entry point.
 */
#pragma once

#include <stdint.h>

/*
 * C entry point, called exactly once from arch/x86_64/entry.asm with the
 * physical address of the bootinfo block (see docs/book/appendix-e-boot-protocol.md).
 * Does not return.
 */
void kmain(uint64_t bootinfo_phys);
