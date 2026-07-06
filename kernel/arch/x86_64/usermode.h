/*
 * usermode.h - arch glue for ring 3 (see syscall_entry.asm).
 */
#pragma once

#include <stdint.h>

#include <compiler.h>

/* Drop into ring 3 at rip with the given user stack. Used once per
 * process launch; never returns. */
NORETURN void user_enter(uint64_t rip, uint64_t rsp);
