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

**Phase 5 underway: the kernel has a disk — PCI enumeration, a VIRTIO 1.2 virtio-blk
driver, and a cached block layer. Next: graphfs, our native graph filesystem.**

Phase 4 delivered a real process model — fork, execve, wait — running in ring 3 with full
fault isolation.

The system boots from a raw disk image in QEMU: our two-stage BIOS bootloader (A20, E820,
unreal-mode ELF load, long-mode transition) hands off to a higher-half kernel at
`0xffffffff80000000`. The kernel owns its descriptor tables (GDT/TSS with a double-fault
IST stack, 256-vector IDT with register-dumping exception handlers), takes timer and
keyboard interrupts through a remapped PIC, and manages memory end to end: an
E820-seeded physical frame allocator, kernel-built page tables with a full physical
direct map and a W^X kernel image (NX enforced everywhere data lives), and a slab
`kmalloc`. On top of that runs a preemptive round-robin scheduler (kernel threads, 10 ms
timeslices, timed sleep, block/wake, pthread-style join) — and on top of *that*, a
Unix-shaped userspace: statically linked **ELF64 executables, written in C**, validated
by a host-tested loader, mapped into per-process address spaces with per-segment W^X
permissions, talking back through `syscall` using the Linux x86_64 ABI convention. Init
**forks a child, the child execve()s a second program (argv delivered on a proper SysV
stack), and init reaps it with wait4** — the boot below shows both programs printing
from ring 3. A faulting process dies alone: a hardware exception in ring 3 kills that
process with the matching Linux signal (`SIGSEGV`, `SIGILL`, ...), reported to the parent
through wait4, while the kernel and every other process keep running — the boot below
shows two deliberately crashed children being reaped. Init's exit status 0 asserts twenty
checks: segment integrity, register preservation across syscalls, every error path
(`-ENOSYS`/`-EBADF`/`-EFAULT`/`-ECHILD`/`-ENOENT`), the fork/exec/wait round trip, and
fault isolation itself.

Phase 5 has begun on the storage stack: the kernel scans the PCI bus, drives a **modern
virtio-blk device implemented against the VIRTIO 1.2 specification** (capability-based
transport, split virtqueues with host-tested ring bookkeeping), and reads and writes the
disk through a write-through block cache — proven at every boot by a write/readback/
restore self-test against the real device. The filesystem it will carry is not ext2 but
**graphfs, a from-scratch property-graph filesystem**: files as nodes, typed named edges
as the structure, the POSIX tree as just one subgraph — the foundation for the AI-native
semantic layer in Phase 6.

```
Hallucinate OS v0.5.0 (x86_64)
cpu: GDT/TSS loaded, IDT ready (256 vectors), PIC remapped and masked
e820: 7 entries
memory: 255 MiB usable
pmm: 255 MiB managed, 254 MiB free, bitmap 7 KiB at 0x121000
vmm: kernel page tables active, 4096 MiB direct-mapped at 0xffff800000000000
heap: slab allocator ready
sched: online, round-robin, 10 ms timeslice
syscall: SYSCALL/SYSRET ready (Linux x86_64 ABI numbering)
timer: 100 Hz, ticking (slept 3 ticks)
pci: 00:00.0 8086:1237 class 06.00
pci: 00:01.0 8086:7000 class 06.01
pci: 00:01.1 8086:7010 class 01.01
pci: 00:01.3 8086:7113 class 06.80
pci: 00:02.0 1234:1111 class 03.00
pci: 00:03.0 8086:100e class 02.00
pci: 00:04.0 1af4:1042 class 01.00
pci: 7 functions
virtio-blk: 16 MiB (32768 sectors), queue size 128
block: virtio-blk, 16 MiB (4096 blocks of 4096), cache 256 KiB
block: selftest passed (write/readback/restore)
selftest: sched interleave "abcabcabcabc"
selftest: passed (701 assertions)
user: launching init (embedded ELF, 13416 bytes)
hello from ring 3
hello from execve
trap: user fault: #PF page fault at rip=0x400444 (error 0x6, cr2=0xffffffff80000000)
user: pid 3 (/bin/init) killed by signal 11
trap: user fault: #UD invalid opcode at rip=0x400434 (error 0)
user: pid 4 (/bin/init) killed by signal 4
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
| 4 | Ring 3 userspace, ELF loader, processes | ✅ done |
| 5 | virtio-blk, VFS, graphfs — our native graph filesystem | 🔨 in progress |
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
kernel/      the kernel: arch/x86_64/, drivers/, block/, mm/, sched/, proc/, lib/, and core
user/        userspace: init.c, crt0.asm, syscall wrappers, linker script
tools/       mkimage.py — assembles and validates the disk image
tests/       host unit tests and the QEMU boot harness
```

## Authorship

Built with [Claude Code](https://claude.com/claude-code). Human contributions: project
direction and decisions only — no code.
