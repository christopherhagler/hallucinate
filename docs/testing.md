# Testing

`make check` is the gate for every commit. It runs two automated suites; a third level
(in-kernel self-tests) executes inside the second. All three accumulate over the life of
the project so regressions in earlier phases are caught forever.

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
On success it prints:

```
selftest: passed (N assertions)
```

A failed check panics, which the integration harness detects. Self-tests are cheap by
design (they run on every boot) — heavy stress tests belong at level 1 or behind a
dedicated integration scenario.

## Level 3 — QEMU integration test (`make check-boot`)

`tests/run_qemu.py` boots the actual disk image headless:

```
qemu-system-x86_64 -m 256M -drive file=disk.img,format=raw \
    -serial stdio -display none -monitor none -no-reboot
```

It asserts that these markers appear on the serial console **in order**:

1. `Hallucinate OS` (banner)
2. `e820:` (memory map parsed from bootinfo)
3. `selftest: passed` (level 2 green)
4. `boot: complete`

and fails immediately if `PANIC` (kernel) or `ERR:` (bootloader) appears, or on a 30 s
timeout. On failure the full serial transcript is printed. The bootloader and kernel are
written so that every fatal path emits one of the failure patterns — a hang without
output is the only failure mode the harness can attribute solely to a timeout.

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
