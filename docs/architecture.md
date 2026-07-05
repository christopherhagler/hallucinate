# Architecture Overview

Hallucinate OS is a from-scratch monolithic kernel for x86_64, written in C11 and NASM,
booted by its own two-stage BIOS bootloader. This document describes the system as it
exists today and the structure the rest of the roadmap builds on. Companion documents:

- [boot-protocol.md](boot-protocol.md) — bootloader ↔ kernel contract (versioned)
- [memory-map.md](memory-map.md) — physical and virtual address space layout
- [testing.md](testing.md) — the three-level test strategy behind `make check`

## Design principles

1. **Everything from scratch.** No third-party code in the boot path or kernel. Where an
   external interface is implemented (ELF, VIRTIO, the Linux syscall ABI), it is
   implemented against the published specification.
2. **Arch split from day one.** All x86_64-specific code lives in `kernel/arch/x86_64/`;
   the rest of the kernel is arch-neutral C. A future aarch64 port adds a directory, not
   a rewrite.
3. **Deterministic kernel, AI as a service.** AI integration (roadmap Phase 6) happens
   via a privileged userspace daemon and device nodes — never inside kernel space.
4. **Complete or absent.** Features are fully implemented within their documented scope
   or not merged. Unsupported operations fail explicitly.

## Source tree

```
boot/                     two-stage BIOS bootloader (NASM, flat binaries)
kernel/
  arch/x86_64/            entry stub, port I/O, CPU intrinsics (grows: GDT, IDT, paging)
  drivers/                serial (16550), VGA text (grows: virtio, keyboard)
  lib/                    freestanding C library: string.c, fmt.c (vsnprintf)
  include/                public kernel headers (bootinfo.h, memlayout.h, ...)
  console.c               console multiplexer (serial + VGA)
  kprintf.c               formatted kernel logging
  panic.c                 fatal-error path (file/line, halt)
  selftest.c              boot-time assertion suite
  main.c                  kmain: bring-up sequence
  linker.ld               higher-half link script
tools/mkimage.py          disk image assembler + boot-protocol validator
tests/host/               unit tests, compiled for macOS under ASan/UBSan
tests/run_qemu.py         headless QEMU integration harness
docs/                     this documentation
```

## Boot flow

```
BIOS → stage1 (MBR, LBA read) → stage2 (A20, E820, unreal-mode kernel load,
protected mode, page tables, long mode, ELF64 load) → _start → kmain
```

Details and the exact CPU/register contract are in [boot-protocol.md](boot-protocol.md).

## Kernel bring-up sequence (`kmain`)

1. `console_init()` — serial COM1 (115200 8N1, with loopback self-test; a missing UART is
   tolerated) and VGA text mode. All output goes to both sinks.
2. Validate the bootinfo block (magic, version, E820 sanity); panic on any mismatch.
3. Print the E820 memory map and total usable memory.
4. `selftest_run()` — in-kernel assertions over the freestanding library.
5. `boot: complete` marker, then halt. Phase 2 (GDT/IDT, interrupts, memory management)
   continues from this point.

## Key subsystems (current)

### Console and logging

`console.c` fans out to the serial and VGA drivers; serial gets `\r` before `\n`.
`kprintf` formats into a bounded stack buffer via the kernel's own full-featured C99
`vsnprintf` (`lib/fmt.c`) — the same code that runs under sanitizers in the host test
suite. The serial driver's transmit wait is bounded and self-disabling so a wedged UART
can never hang the kernel.

### Panic

`panic(fmt, ...)` prints `PANIC: file:line: message` to all consoles and halts
interrupts-off. `KASSERT` builds on it. The integration harness fails a boot the moment
`PANIC` appears on the serial line. (Register dump and stack backtrace land with the
exception infrastructure in Phase 2.)

### Freestanding library

`lib/string.c` and `lib/fmt.c` are UB-free, byte-exact implementations used by the whole
kernel. They are the template for how all arch-neutral kernel logic is developed: the
identical source files compile on macOS (with symbol renaming to avoid libc collisions)
and run under AddressSanitizer/UBSan in `tests/host/`.

## Toolchain

Development host is macOS on Apple Silicon; no cross-compiler build is required:

- **clang** (Xcode CLT) with `--target=x86_64-elf -ffreestanding -mcmodel=kernel
  -mno-red-zone` and no SSE/AVX, `-Wall -Wextra -Werror`.
- **ld.lld** (brew `lld`) with a custom higher-half link script.
- **nasm** for the bootloader (flat binary) and kernel entry (elf64).
- **qemu-system-x86_64** for execution; TCG emulation on Apple Silicon.
- **clang-format / clang-tidy** (brew `llvm`) enforced via `make format-check` and
  `make tidy`.

## Roadmap shape

The phase plan (see README) drives toward three pillars:

1. **A real OS**: memory management, scheduling, userspace, VFS/ext2, GUI.
2. **Linux binary compatibility**: a Linux syscall ABI personality layer so unmodified
   static musl binaries (busybox first) run natively — the same approach FreeBSD and
   managarm use. Syscall coverage will be tracked in `docs/syscalls.md`.
3. **AI as a system service**: a guest daemon (`aid`) bridged to a Claude API helper on
   the host over virtio-serial, exposed to all processes via `/dev/ai` and dedicated
   syscalls, with AI-native interfaces (natural-language shell, semantic open) on top.
