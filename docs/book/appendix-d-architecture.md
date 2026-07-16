# Architecture Overview

Hallucinate OS is a from-scratch monolithic kernel for x86_64, written in C11 and NASM,
booted by its own two-stage BIOS bootloader. This document describes the system as it
exists today and the structure the rest of the roadmap builds on. Companion documents:

- [Appendix E](appendix-e-boot-protocol.md) — bootloader ↔ kernel contract (versioned)
- [Appendix F](appendix-f-memory-map.md) — physical and virtual address space layout
- [Appendix G](appendix-g-scheduling.md) — threads, context switch, preemption
- [Appendix H](appendix-h-userspace.md) — ring 3, syscall ABI, ELF loading, the process model
- [Appendix I](appendix-i-storage.md) — PCI, virtio-blk, the block layer
- [Appendix J](appendix-j-vfs.md) — the VFS: mounts, open files, fd tables, devfs
- [Appendix K](appendix-k-graphfs.md) — graphfs, the native filesystem's on-disk format
- [Appendix L](appendix-l-testing.md) — the three-level test strategy behind `make check`

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
  arch/x86_64/            entry stub, GDT/TSS, IDT + trap dispatch (isr.asm),
                          IRQ layer, 8259 PIC, 4-level paging, context switch
                          (ctx.asm), port I/O, CPU intrinsics
  drivers/                serial (16550), VGA text, 8254 PIT, PS/2 keyboard
                          (kbd_map.c: pure scancode translation, host-tested),
                          pci.c (bus scan, config space), virtio_pci.c
                          (VIRTIO 1.2 modern transport), virtq_core.c (pure
                          split-ring bookkeeping, host-tested), virtio_blk.c
  block/                  block device layer: 4 KiB blocks, write-through
                          LRU cache over the registered driver
  fs/                     graphfs_core.c (pure on-disk format engine,
                          host-tested), vfs.c (mounts, open files, graphfs
                          vnode ops), vfs_path.c (pure path normalization,
                          host-tested), devfs.c (/dev/console)
  mm/                     pmm_core.c + pmm.c (frame allocator),
                          vmm.c (kernel address space), heap_core.c + kmalloc.c (slab)
  sched/                  sched_core.c (policy, host-tested) + sched.c
                          (threads, sleep/wake, join, preemption),
                          mutex.c (sleeping FIFO mutex)
  proc/                   proc_core.c (process table, host-tested) +
                          process.c (fork/execve/wait4/exit, fd tables),
                          elf_load.c (segment loader), syscall.c (dispatch),
                          uaccess.c (user pointer validation and copies)
  lib/                    freestanding C library: string.c, fmt.c (vsnprintf),
                          elf64.c (pure ELF64 validator, host-tested)
  include/                public kernel headers (bootinfo.h, memlayout.h, ...)
  console.c               console multiplexer (serial + VGA)
  kprintf.c               formatted kernel logging
  panic.c                 fatal-error path (file/line, halt)
  selftest.c              boot-time assertion suite
  main.c                  kmain: bring-up sequence
  linker.ld               higher-half link script (W^X section symbols)
user/                     userspace: init.c, hello.c, crt0.asm, syscall.h
                          wrappers, user.ld (static ELF64 at 0x400000, W^X
                          segments); installed on fs.img at /bin
tools/mkimage.py          boot disk image assembler + boot-protocol validator
tools/graphfs_mkfs.c      builds fs.img (graphfs) from host files
tools/graphfs_fsck.c      offline graphfs invariant checker (make check gate)
tests/host/               unit tests, compiled for macOS under ASan/UBSan
tests/run_qemu.py         headless QEMU integration harness
docs/book/                this documentation (chapters + these appendices)
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
2. `gdt_init()` / `idt_init()` / `pic_init()` / `irq_init()` — kernel descriptor tables
   (TSS with a dedicated double-fault IST stack), 256 interrupt stubs feeding a trap
   dispatcher (unhandled traps dump all registers and panic), PICs remapped to vectors
   0x20–0x2F and masked.
3. Validate the bootinfo block (magic, version, E820 sanity); panic on any mismatch.
4. `pmm_init()` — bitmap frame allocator seeded from E820 (see Appendix F).
5. `vmm_init()` — kernel page tables: HHDM direct map, W^X kernel image, NX, no
   identity map; page-fault handler with error decoding.
6. `kmalloc_init()` — slab heap over PMM frames.
7. `sched_init()` — the boot context becomes thread 0, the idle thread is created,
   and the scheduler hooks the timer tick (see Appendix G).
8. `syscall_init()` — SYSCALL/SYSRET MSRs (see Appendix H).
9. `timer_init(100)` / `keyboard_init()`, then interrupts on; the boot proves the timer
   ticks by sleeping on it.
10. `pci_init()` / `virtio_blk_init()` / `block_selftest()` — bus scan, virtio-blk
    bring-up per VIRTIO 1.2, and a write/readback/restore round trip through the real
    device (see Appendix I).
11. `vfs_init()` — mount the graphfs root read-only from the block device, initialize
    devfs at `/dev` (see Appendix J). Panics if there is no disk or no valid filesystem.
12. `selftest_run()` — in-kernel assertions over the lib, traps (int3), PMM, VMM
    protections, heap, and scheduler (thread interleaving, sleep, preemption, join).
13. `process_run_init()` — `/bin/init` is read off the filesystem, validated, loaded
    into a fresh user address space, and runs in ring 3 as pid 1 with fds 0/1/2 open
    on `/dev/console`. Init exercises the whole process model and the VFS read side —
    it forks, the child execve()s `/bin/hello` from disk, init reaps it with wait4,
    two deliberately crashed children prove that a ring 3 fault kills only the
    faulting process, and every file syscall is probed against known filesystem
    contents — then exits; the kernel verifies the process table is empty and not
    one physical frame leaked.
14. `boot: complete`, then an interactive keyboard echo loop (the pre-shell placeholder).

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

1. **A real OS**: memory management, scheduling, userspace, VFS + graphfs, GUI.
2. **Linux binary compatibility**: a Linux syscall ABI personality layer so unmodified
   static musl binaries (busybox first) run natively — the same approach FreeBSD and
   managarm use. Syscall coverage will be tracked in `docs/syscalls.md`.
3. **AI as a system service**: a guest daemon (`aid`) bridged to a Claude API helper on
   the host over virtio-serial, exposed to all processes via `/dev/ai` and dedicated
   syscalls, with AI-native interfaces (natural-language shell, semantic open) on top.
