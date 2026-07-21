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

**Phase 5 complete: a full read/write storage stack, from PCI enumeration to POSIX
semantics. graphfs — our from-scratch copy-on-write property-graph filesystem — is
mounted read-write at `/` through a VFS with devfs at `/dev`; userspace creates,
writes, links, renames, and unlinks files with the Linux ABI, and after every boot
test an fsck proves the written image is still perfectly consistent. Phase 6 (AI as
a system service) is now in progress: anonymous pipes (`pipe(2)`) landed first — a
blocking producer/consumer byte stream with real reader/writer wakeups, the first
VFS object that isn't graphfs or devfs — as the IPC groundwork the AI daemon and a
future shell both need.**

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
shows two deliberately crashed children being reaped. Init's exit status 0 asserts
104 checks: segment integrity, register preservation across syscalls, every error
path, the fork/exec/wait round trip, fault isolation itself, the whole file surface
— read side and write path — and pipes: a blocking round trip across a real `fork`,
`EOF` once every writer has closed, `-EPIPE` once every reader has.

Phase 5's storage stack is real end to end. The kernel scans the PCI bus, drives a
**modern virtio-blk device implemented against the VIRTIO 1.2 specification**
(capability-based transport, split virtqueues with host-tested ring bookkeeping), and
reads the disk through a write-through block cache — proven at every boot by a
write/readback/restore self-test against the real device. On it lives not ext2 but
**graphfs, a from-scratch property-graph filesystem**: files as nodes, typed named
edges as the structure, the POSIX tree as just one subgraph — the foundation for the
AI-native semantic layer in Phase 6. On disk it is copy-on-write and self-checksumming
(crc32c carried in every pointer to a metadata block), so the image is consistent at
every instant — no journal, no fsck-on-boot — and the format engine is one pure C
module shared verbatim by the kernel, the `graphfs_mkfs` image builder, and the
`graphfs_fsck` checker that gates `make check`.

Above it, a **VFS** owns the namespace: compile-time mounts (graphfs read-write at `/`,
devfs at `/dev`), an ops-vtable open-file layer with POSIX description sharing across
fork, per-process fd tables, lexical path normalization (pure, host-tested), and the
kernel's first sleeping lock serializing the disk. The file syscalls — `read`, `write`,
`open` (with `O_CREAT`/`O_EXCL`/`O_TRUNC`/`O_APPEND`), `close`, `fstat`, `lseek`,
`getdents64`, `mkdir`, `rmdir`, `link`, `unlink`, `rename`, `fsync` — keep the Linux
x86_64 ABI bit-for-bit (144-byte `struct stat`, real `linux_dirent64` records, the
full errno vocabulary). **Every mutation is one copy-on-write transaction, durable
before the syscall returns**: creation is atomic (no crash window ever leaves an
orphan), rename replaces with POSIX semantics and refuses subtree cycles, hard links
share nodes, and an in-kernel stress test plus a post-boot `graphfs_fsck` prove on
every single boot that the written image stayed consistent and leaked nothing.
`/dev/console` blocks on the keyboard for input, init starts with fds 0/1/2 on it,
and **execve loads ELFs through the VFS**: the embedded-blob scaffold from Phase 4 is
deleted, and the boot proves init came off the graphfs image.

```
Hallucinate OS v0.5.0 (x86_64)
cpu: GDT/TSS loaded, IDT ready (256 vectors), PIC remapped and masked
boot: BIOS drive 0x80, boot protocol v1
e820: 7 entries
memory: 255 MiB usable
pmm: 255 MiB managed, 254 MiB free, bitmap 7 KiB at 0x128000
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
vfs: graphfs root mounted rw (gen 7, 4080/4096 blocks free, 1018/1024 nodes free)
vfs: devfs at /dev (console)
selftest: sched interleave "abcabcabcabc"
selftest: pipes ok (blocking write/read, EOF, EPIPE)
selftest: fs write path ok (create/write/rename/unlink cycles)
selftest: passed (848 assertions)
user: launching init (/bin/init from disk, 17688 bytes)
hello from ring 3
hello from execve
trap: user fault: #PF page fault at rip=0x401384 (error 0x6, cr2=0xffffffff80000000)
user: pid 3 (/bin/init) killed by signal 11
trap: user fault: #UD invalid opcode at rip=0x401374 (error 0)
user: pid 4 (/bin/init) killed by signal 4
user: console open via /dev/console ok
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
| 5 | virtio-blk, VFS, graphfs — our native graph filesystem, full write path | ✅ done |
| 6 | AI service layer: `/dev/ai`, AI daemon, natural-language shell | 🚧 in progress |
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
make usbimg   # build the same image and print USB-flashing instructions
```

`build/disk.img` is already a raw, BIOS-bootable MBR image — flashing it to a USB stick
and booting real hardware is covered end to end in
[docs/book/appendix-m-real-hardware.md](docs/book/appendix-m-real-hardware.md), including
a safety-checked `tools/flash_usb.sh`.

## Testing

`make check` must pass before every commit. It runs:

- **Host unit tests** — the kernel's pure cores (freestanding library, allocators,
  scheduler policy, keyboard translation, ELF validator, virtqueue bookkeeping, graphfs
  including its whole write path, path normalization) compiled for macOS and run under
  AddressSanitizer/UBSan.
- **Boot integration tests** — two headless QEMU boots of the real disk image, asserting
  that the expected markers appear on the serial console in order and that no panic
  occurs: one with the filesystem disk attached (`graphfs_fsck` then verifies the image
  the guest wrote to, so it's also a crash-consistency test of the write path), and one
  with **no disk at all** — the state real hardware is in before an AHCI/NVMe driver
  exists, proving the kernel degrades instead of panicking (docs/book/appendix-m).
- **Filesystem check** — `graphfs_fsck` verifies every invariant of the built `fs.img`.

A third level, in-kernel self-tests, runs on every boot (including a filesystem write
stress with exact free-space accounting) and is asserted by the integration test.

## Layout

```
boot/        stage1.asm (MBR), stage2.asm (A20, E820, long mode, ELF loader)
kernel/      the kernel: arch/x86_64/, drivers/, block/, fs/, mm/, sched/, proc/, lib/, and core
user/        userspace: init.c, hello.c, crt0.asm, syscall wrappers, linker script
tools/       mkimage.py (boot disk), graphfs_mkfs.c (fs.img), graphfs_fsck.c (checker)
tests/       host unit tests and the QEMU boot harness
```

## Authorship

Built with [Claude Code](https://claude.com/claude-code). Human contributions: project
direction and decisions only — no code.
