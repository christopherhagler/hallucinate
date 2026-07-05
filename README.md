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

**Phase 1 complete: two-stage bootloader and higher-half x86_64 kernel.**

The system boots from a raw disk image in QEMU: a 512-byte MBR stage 1 loads stage 2, which
enables the A20 line, collects the BIOS E820 memory map, loads the kernel ELF above 1 MiB
using unreal mode, builds the initial page tables, enters 64-bit long mode, and jumps to a
higher-half kernel at `0xffffffff80000000`. The kernel brings up serial and VGA text
consoles, validates the boot protocol, prints the memory map, and runs a boot-time
self-test suite.

```
Hallucinate OS v0.1.0 (x86_64)
boot: BIOS drive 0x80, boot protocol v1
e820: 7 entries
e820:  [0x0000000000 - 0x000009fbff] usable
...
memory: 255 MiB usable
selftest: passed (12 assertions)
boot: complete
```

### Roadmap

| Phase | Scope | Status |
|---|---|---|
| 0 | Toolchain, build system, test harness | ✅ done |
| 1 | Two-stage bootloader, long mode, higher-half kernel, consoles | ✅ done |
| 2 | GDT/IDT, exceptions, timer, physical + virtual memory, kernel heap | next |
| 3 | Kernel threads and scheduling | planned |
| 4 | Ring 3 userspace, ELF loader, processes | planned |
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
kernel/      the kernel: arch/x86_64/, drivers/, lib/, and core
tools/       mkimage.py — assembles and validates the disk image
tests/       host unit tests and the QEMU boot harness
```

## Authorship

Built with [Claude Code](https://claude.com/claude-code). Human contributions: project
direction and decisions only — no code.
