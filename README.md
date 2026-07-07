# Hallucinate OS

**An operating system written entirely by AI — zero lines of human-written code.**

Hallucinate OS is an experiment: how well can an AI build a complete operating system from
scratch? Every line in this repository — the bootloader, the kernel, the build system, the
test harnesses, the tooling, and this README — was written by [Claude](https://claude.com)
via Claude Code. The human role is limited to setting goals, making product decisions, and
approving plans; the AI does all of the design, implementation, debugging, and testing.

This is not a Linux distribution. The bootloader and kernel are built from first principles:
raw x86 assembly at the MBR, a hand-rolled long-mode transition, a from-scratch C kernel.
The long-term goal is a self-hosting, AI-native OS that can run unmodified Linux binaries
through its own syscall compatibility layer.

## The experiment

Two questions drive the project:

1. **Capability** — can an AI carry a months-long systems project across the full stack,
   from 16-bit real-mode assembly up to a GUI, at professional engineering quality
   (complete features only, `-Werror`, three levels of automated testing, documented
   interfaces)?
2. **AI-native design** — what does an OS look like when AI is a first-class system
   service? Planned: an AI daemon with kernel introspection, exposed to every process via
   `/dev/ai` and dedicated syscalls, plus AI-native interfaces like a natural-language
   shell.

## Current status

**Phase 3 complete; Phase 4 underway: the first userspace program runs in ring 3.**

The system boots from a raw disk image in QEMU: our two-stage BIOS bootloader (A20, E820,
unreal-mode ELF load, long-mode transition) hands off to a higher-half kernel at
`0xffffffff80000000`. The kernel owns its descriptor tables (GDT/TSS with a double-fault
IST stack, 256-vector IDT with register-dumping exception handlers), takes timer and
keyboard interrupts through a remapped PIC, and manages memory end to end: an
E820-seeded physical frame allocator, kernel-built page tables with a full physical
direct map and a W^X kernel image (NX enforced everywhere data lives), and a slab
`kmalloc`. On top of that runs a preemptive round-robin scheduler (kernel threads, 10 ms
timeslices, timed sleep, pthread-style join) — and on top of *that*, userspace: a
statically linked **ELF64 executable, written in C**, is validated by a host-tested
loader, mapped into its own address space with per-segment W^X permissions, and dropped
into ring 3, talking back through `syscall` using the Linux x86_64 ABI convention. The
two `user:`-prefixed lines below are printed by that C program; its exit status 0
asserts ten checks — zero-filled `.bss`, initialized writable `.data`, `getpid()`,
registers preserved across syscalls, and the `-ENOSYS`/`-EBADF`/`-EFAULT` error paths
among them.

```
Hallucinate OS v0.3.0 (x86_64)
cpu: GDT/TSS loaded, IDT ready (256 vectors), PIC remapped and masked
e820: 7 entries
memory: 255 MiB usable
pmm: 255 MiB managed, 254 MiB free, bitmap 7 KiB at 0x117000
vmm: kernel page tables active, 4096 MiB direct-mapped at 0xffff800000000000
heap: slab allocator ready
sched: online, round-robin, 10 ms timeslice
syscall: SYSCALL/SYSRET ready (Linux x86_64 ABI numbering)
timer: 100 Hz, ticking (slept 3 ticks)
selftest: sched interleave "abcabcabcabc"
selftest: passed (701 assertions)
user: launching init (embedded ELF, 13232 bytes)
hello from ring 3
user: C init: .data .bss .rodata ok
user: init exited (status 0)
boot: complete
```

### Roadmap

| Phase | Scope | Status |
|---|---|---|
| 0 | Toolchain, build system, test harness | ✅ done |
| 1 | Two-stage bootloader, long mode, higher-half kernel, consoles | ✅ done |
| 2 | GDT/IDT, exceptions, timer, physical + virtual memory, kernel heap | ✅ done |
| 3 | Kernel threads and scheduling | ✅ done |
| 4 | Ring 3 userspace, ELF loader, processes | 🔨 in progress |
| 5 | virtio-blk, VFS, ext2 | planned |
| 6 | AI service layer: `/dev/ai`, AI daemon, natural-language shell | planned |
| 7 | Linux syscall compatibility (static musl → busybox → dynamic) | planned |
| 8 | Framebuffer GUI with AI-integrated command palette | planned |
| 9 | Networking, TLS, local inference | planned |
| 10 | aarch64 port | planned |

## Building and running

Requirements (macOS): Xcode command-line tools (`clang`), plus:

```sh
brew install nasm lld llvm qemu
```

```sh
make          # build build/disk.img
make run      # boot it in QEMU (VGA window + serial on stdio)
make check    # run all test suites
```

## Testing

`make check` must pass before every commit. It runs:

- **Host unit tests** — the kernel's freestanding library (string, printf) compiled for
  macOS and run under AddressSanitizer/UBSan.
- **Boot integration test** — a headless QEMU boot of the real disk image, asserting that
  the expected markers appear on the serial console in order and that no panic occurs.

A third level, in-kernel self-tests, runs on every boot and is asserted by the integration
test.

## Layout

```
boot/        stage1.asm (MBR), stage2.asm (A20, E820, long mode, ELF loader)
kernel/      the kernel: arch/x86_64/, drivers/, mm/, sched/, proc/, lib/, and core
user/        userspace: init.c, crt0.asm, syscall wrappers, linker script
tools/       mkimage.py — assembles and validates the disk image
tests/       host unit tests and the QEMU boot harness
```

## Authorship

Built with [Claude Code](https://claude.com/claude-code). Human contributions: project
direction and decisions only — no code.
