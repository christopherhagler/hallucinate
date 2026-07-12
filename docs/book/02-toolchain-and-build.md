# Chapter 2 — The Toolchain and the Build

You cannot write a kernel until you can *build* one, and building a kernel is
the first place the host operating system stops helping you. Your compiler, by
default, produces programs that expect a C runtime, a dynamic linker, a stack
set up by the OS, and a libc. A kernel has none of those. This chapter is about
convincing the toolchain to emit code for a machine that has nothing — and about
the disk image that machine's firmware will actually load.

## 2.1 Freestanding, and what it costs you

The C standard distinguishes a *hosted* implementation (the normal one, with a
full library and a `main` called by a runtime) from a *freestanding* one, where
almost nothing is guaranteed except the language core and a handful of headers
(`<stdint.h>`, `<stddef.h>`, `<stdbool.h>`, and a few others). `-ffreestanding`
selects the latter. Concretely it means: no `libc`, no `malloc`, no `memcpy`
unless you wrote it, no `printf`, and no assumption that `main` is special.

Look at the kernel's compile flags (`Makefile`):

```make
KERNEL_CFLAGS := --target=x86_64-elf -std=c11 -ffreestanding -fno-builtin \
    -fno-stack-protector -fno-pic -mcmodel=kernel -mno-red-zone \
    -mno-mmx -mno-sse -mno-sse2 -mno-avx \
    -Wall -Wextra -Werror -O2 -g -MMD -MP \
    -Ikernel/include -Ikernel
```

Every flag here is load-bearing. Take them one at a time, because each
corresponds to an assumption the host toolchain makes that is false on bare
metal:

- **`--target=x86_64-elf`** — cross-compile for a bare x86_64 ELF target rather
  than macOS Mach-O. Clang is a cross-compiler out of the box; on Apple Silicon
  you are compiling for an architecture your build machine cannot even execute
  natively, which is exactly why the host tests (Chapter 14) compile the *pure*
  code a second time for the Mac.
- **`-ffreestanding -fno-builtin`** — freestanding, and also stop the compiler
  from "helpfully" recognizing a loop as `memset` and calling a `memset` you
  have not linked, or replacing your `memcpy` with a builtin. In a kernel you
  must own those symbols; `-fno-builtin` keeps the compiler's hands off them.
- **`-fno-stack-protector`** — the stack-smashing canary calls
  `__stack_chk_fail`, a libc function that does not exist here. Off.
- **`-fno-pic -mcmodel=kernel`** — the kernel is linked to run at a fixed, very
  high virtual address (`0xffffffff80000000`, the top 2 GiB). The "kernel" code
  model tells the compiler it may assume everything lives in that top-2-GiB
  window, so it can use efficient RIP-relative and 32-bit-signed addressing
  instead of full 64-bit position-independent sequences. `-fno-pic` because
  there is no dynamic loader to relocate anything.
- **`-mno-red-zone`** — this one is subtle and mandatory. The System V ABI
  grants leaf functions a 128-byte "red zone" below `rsp` that they may use
  without adjusting the stack pointer, on the promise that nothing will
  asynchronously clobber it. In userspace that promise holds. In a kernel,
  **an interrupt can fire at any instruction**, and the CPU pushes the
  interrupt frame right where `rsp` points — straight through the red zone. If
  kernel code used the red zone, an interrupt would silently corrupt a live
  local variable. So the red zone is disabled kernel-wide. This is the kind of
  bug that works for months and then destroys you under load; the flag is how
  you never have it.
- **`-mno-sse -mno-sse2 -mno-mmx -mno-avx`** — the kernel does not save and
  restore SSE/AVX register state across context switches (that machinery, "lazy
  FPU switching," is deliberately later work). If the compiler were allowed to
  use XMM registers to, say, copy a struct, a context switch mid-copy would
  corrupt another thread's floating-point state. Forbidding SIMD in the kernel
  makes the "we don't save FPU state" shortcut *safe* rather than a latent bug.
  Userspace is built with the same restriction for the same reason (`USER_CFLAGS`).
- **`-Wall -Wextra -Werror`** — warnings are errors. In a domain where the
  compiler's static analysis is one of your few remaining safety nets, you do
  not get to ignore it.

Notice the pattern: nearly every flag disables a convenience the host ABI
provides for free, because that convenience assumes an environment the kernel
does not have. Understanding *why* each is off is understanding the boundary
between hosted and freestanding code.

## 2.2 The linker script: placing code in an address space that does not exist yet

The compiler emits object files; the linker decides where their sections live in
the final address space. For a normal program the linker uses a default script
and you never think about it. A kernel needs a custom one, because the kernel
runs at a virtual address (`0xffffffff80000000`) that is different from the
physical address it is *loaded* at (`0x100000`, the 1 MiB mark) — a distinction
that will make sense after Chapter 3, but that the linker script has to encode
now.

`kernel/linker.ld` does two jobs. First, it sets the entry point and the load
vs. run addresses. Second — and this is the professional touch — it emits
**symbols at every section boundary**, page-aligned:

```
_text_start   .. _text_end     (executable, read-only)
_rodata_start .. _rodata_end    (read-only, no-execute)
_data_start   .. _data_end      (read-write, no-execute; .data and .bss)
```

Those symbols are why the kernel can later enforce **W^X** (write-xor-execute):
`vmm_init()` reads `_text_start`/`_text_end` and maps that range read-execute
but *not* writable, maps rodata read-only-no-execute, and maps data
writable-no-execute. The linker script and the page-table code are two ends of
one contract — the boundaries are aligned by the script precisely so the page
tables (4 KiB granularity) can protect them exactly. A boot self-test later
verifies the protections actually took. This is a recurring theme: a security
property (no writable-executable memory) is realized as a collaboration between
the build system, the linker, and runtime code, and *checked* at runtime rather
than assumed.

## 2.3 Assembling the disk image by hand

The compiler and linker give you two artifacts: flat binaries for the two
bootloader stages (`nasm -f bin`) and an ELF for the kernel. The firmware,
however, does not load ELFs or link scripts — it loads *sectors off a disk*. So
`tools/mkimage.py` assembles a raw disk image with a specific on-disk layout
(the full contract is `docs/boot-protocol.md`):

| LBA | Contents |
|-----|----------|
| 0 | Stage 1 — exactly 512 bytes, ending in the `0xAA55` boot signature |
| 1..N | Stage 2, sector-padded (N ≤ 127 so stage 1 can load it in one BIOS call) |
| N+1.. | The kernel ELF, sector-padded |

There is a detail here worth stealing. Neither bootloader stage hard-codes where
the next piece lives, because at assembly time you do not yet know how big stage
2 or the kernel will be. Instead each stage embeds a **marker string** — `"HB1\0"`
in stage 1, `"HB2\0"` in stage 2 — and `mkimage.py` finds those markers in the
assembled bytes and patches the real geometry in after the fact: stage 2's
sector count into stage 1's disk-address-packet, the kernel's start LBA and
sector count into stage 2. The tool even asserts the bytes around the marker are
what it expects (the DAP header `0x10 0x00`) before patching, so a refactor that
moved the structure cannot silently corrupt the image.

This is build-time metaprogramming done responsibly: the code and the tool share
a versioned contract, the tool validates its assumptions before it writes, and
the whole thing is reproducible from `make`. When you find yourself wanting to
hard-code an offset that "won't change," reach for a marker-and-patch scheme
instead — future-you will change it.

## 2.4 One nasty portability bug worth remembering

The `Makefile` carries a comment that encodes a real debugging scar:

```make
# Explicit rules, not patterns: the kernel's %.o: %.c pattern also
# matches these paths, and GNU make 3.81 (macOS) resolves pattern
# conflicts by order, not specificity.
```

macOS ships GNU make 3.81 (from 2006, for licensing reasons). Modern make picks
the *most specific* matching pattern rule; 3.81 picks by *definition order*.
The user programs need `USER_CFLAGS` (ring 3, no kernel code model), but the
kernel's generic `%.o: %.c` pattern also matches `user/init.c`, and on the old
make it would win — silently compiling userspace with kernel flags. The fix is
to give the user objects **explicit, non-pattern rules** so there is no
ambiguity to resolve. The transferable lesson is not about make; it is that
"works on my machine" and "works on the machine in the project's requirements"
are different claims, and the gap is usually a tool version. Pin your
assumptions to the documented environment, and when a build behaves
mysteriously, check the version of the tool before you doubt your logic.

## 2.5 The build as a whole

Putting it together, `make` walks this pipeline:

```
boot/*.asm      --nasm -f bin-->   stage1.bin, stage2.bin
user/*.c,*.asm  --clang/nasm/lld->  init.elf, hello.elf  (embedded into kernel .rodata)
kernel/**.c,*.asm --clang/nasm-->  *.o  --ld.lld -T linker.ld-->  kernel.elf
                                    |
                     tools/mkimage.py assembles + patches
                                    v
                              build/disk.img
```

The userspace ELFs are currently *embedded into the kernel's `.rodata`*
(`kernel/user_blob.asm` uses NASM's `incbin`) so that Phase 4 could run
processes before a filesystem existed to load them from — a scaffold that Phase
5c removes once the kernel can read `/bin/init` off graphfs. That is
complete-or-absent in action: rather than a fake filesystem, an honest embedded
blob with a clear expiry date.

You now have a toolchain that produces a bootable image for a machine with no
OS. In the next chapter the firmware loads sector 0 and runs it, and we begin
the climb from 16-bit real mode to a 64-bit kernel.
