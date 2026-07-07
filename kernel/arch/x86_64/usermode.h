/*
 * usermode.h - arch glue for ring 3 (see syscall_entry.asm).
 */
#pragma once

#include <stdint.h>

#include <compiler.h>

/*
 * Drop into ring 3 with the complete register state in *f. Both a
 * process's first entry (zeroed frame, rip/rsp/rflags set by the
 * image builder) and fork's child resuming at the parent's syscall
 * return point. Never returns.
 */
struct syscall_frame;
NORETURN void user_frame_enter(const struct syscall_frame *f);
