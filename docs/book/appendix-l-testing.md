# Testing

`make check` is the gate for every commit. It runs host unit tests plus two QEMU
boot integration runs (with and without a disk attached); in-kernel self-tests
are a level of their own that executes inside both boot runs. All of it
accumulates over the life of the project so regressions in earlier phases are
caught forever.

## Level 1 — host unit tests (`make check-host`)

Arch-neutral kernel code is compiled **for macOS** and unit-tested under
`-fsanitize=address,undefined -fno-sanitize-recover=all`. This gives the kernel's core
logic the scrutiny of real sanitizers, which cannot run in freestanding kernel builds.

Mechanics (`Makefile`, "host tests" section):

- The kernel sources under test (`kernel/lib/string.c`, `kernel/lib/fmt.c`) are compiled
  with their public symbols renamed (`-Dmemcpy=hl_memcpy`, …) so they can never collide
  with libc or the sanitizer runtime. Test sources get the same renames, so a plain
  `memcpy(...)` call in a test resolves to the kernel implementation under test.
- `-fno-builtin -D_FORTIFY_SOURCE=0` stops the host compiler from replacing or wrapping
  the calls being tested.
- The framework (`tests/host/test.h`) registers tests with constructor attributes;
  assertion failures are reported with file/line and fail the run without aborting it,
  so one run reports every failure.

Adding a test: create/extend a `tests/host/test_*.c` file, add it (and any new kernel
source) to `HOST_TEST_SRCS` in the Makefile. Any new arch-neutral kernel module (an
allocator, a path resolver, an ELF parser) must arrive with host unit tests.

## Level 2 — in-kernel self-tests

`kernel/selftest.c` runs a boot-time assertion suite over the freestanding library as
compiled by the *real* kernel toolchain (`--target=x86_64-elf`, `-mcmodel=kernel`,
no SSE). This catches codegen- and environment-specific breakage the host build cannot.
It also stress-tests the filesystem write path against the real disk: repeated
create/write/read/rename/unlink cycles through the VFS, asserting after each cycle
that free blocks and nodes return exactly to their starting counts (the same leak
discipline the PMM and heap tests use). On success it prints:

```
selftest: fs write path ok (create/write/rename/unlink cycles)
selftest: passed (N assertions)
```

A failed check panics, which the integration harness detects. Self-tests are cheap by
design (they run on every boot) — heavy stress tests belong at level 1 or behind a
dedicated integration scenario. When there is no root filesystem (see below), `test_fs`
is skipped rather than run against a mount that does not exist:

```
selftest: fs write-path test skipped (no root filesystem)
selftest: passed (N assertions)
```

## Level 3 — QEMU integration tests (`make check-boot`, `make check-boot-nodisk`)

`tests/run_qemu.py` boots the actual disk image headless:

```
qemu-system-x86_64 -m 256M -drive file=disk.img,format=raw \
    -serial stdio -display none -monitor none -no-reboot
```

It asserts that the expected markers appear on the serial console **in order**, and
fails immediately if `PANIC` (kernel) or `ERR:` (bootloader) appears, or on a 30 s
timeout. On failure the full serial transcript is printed. The bootloader and kernel are
written so that every fatal path emits one of the failure patterns — a hang without
output is the only failure mode the harness can attribute solely to a timeout.

Two configurations run, both gated by `make check`:

- **`check-boot`** attaches `fs.img` as a virtio-blk device, the normal case. The
  guest boots a throwaway *copy* and writes to it all boot long — the block
  selftest, the in-kernel fs stress cycles, init's write-path checks. After the
  markers pass, the harness runs `graphfs_fsck` over that written image and fails
  the test unless it is perfectly consistent: **every boot test is also an
  end-to-end crash-consistency test of the write path** (`--fsck`).
  Authoritative marker list: `PASS_MARKERS_WITH_DISK`, one marker per proven
  subsystem, from the banner through memory, scheduling, the in-kernel
  self-tests, and the ring 3 round trip (`hello from ring 3` is printed by user
  code) to `boot: complete`.
- **`check-boot-nodisk`** attaches no `fs.img` at all — the same disk-less state
  real hardware is in before an AHCI/NVMe driver exists (Appendix M). Every
  layer must degrade instead of panicking: `virtio-blk: no device`,
  `block: selftest skipped (no device)`, `vfs: no block device found`,
  `process: no root filesystem, skipping init`, and still `boot: complete`.
  Authoritative marker list: `PASS_MARKERS_NO_DISK`. This is what proves the
  real-hardware smoke test will not simply panic before AHCI exists — checked
  on every commit, not just the day someone has a USB stick in hand.

As the OS grows, new integration scenarios add markers (and eventually scripted input via
the QEMU monitor); existing markers are never removed, only extended.

## Static quality gates

- `make format-check` — clang-format compliance for all C sources/headers (`.clang-format`
  in repo root; LLVM style, 4-space indent, 100 columns).
- `make tidy` — clang-tidy over the kernel with the real kernel flags.
- `-Wall -Wextra -Werror` on every compile, host and target.

## Policy

- `make check` must pass before every commit.
- A bug fix lands with a test that failed before the fix.
- A feature is not done until it is covered at the appropriate level(s): pure logic at
  level 1, boot-visible behavior at level 3, invariants at level 2.
