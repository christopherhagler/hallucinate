# Chapter 0 — Prerequisites, Tools, and the Debug Loop

The other fifteen chapters teach you how this operating system works. This one is
different: it is about the *equipment* — what you need to know before you start,
the tools you will live inside, the reference documents you will consult a hundred
times a day, and above all the **debug loop**, because writing an OS is mostly
debugging one. A book cannot make you a systems programmer; a book plus a working
debug loop and the discipline to use it can. Read this chapter with a terminal
open, and actually run the commands.

## 0.1 What you should already have

This book assumes, and you should honestly confirm, the following before Chapter
1 will land:

- **C, fluently.** Pointers, pointer arithmetic, structs and their padding,
  `volatile`, `union`, function pointers, the difference between a declaration and
  a definition, how the stack works at the frame level. You do not need to be an
  expert; you need to not be *looking these up*.
- **The ability to read x86-64 assembly, and to write a little.** You will not
  write much — this codebase is ~95% C — but the parts that are assembly
  (`ctx.asm`, `syscall_entry.asm`, the boot stages) are exactly the parts where
  the machine is most exposed, and you must be able to follow them
  instruction by instruction. If `mov rax, [rbx+8]`, `push`/`pop`, `call`/`ret`,
  and the caller/callee-saved register split are not immediate to you, spend a
  weekend with a NASM tutorial first. You do **not** need to memorize the
  instruction set — you need to be able to read a 40-line stub and look up the
  three instructions you do not recognize.
- **A mental model of virtual memory and privilege rings** at the "I have heard of
  page tables and ring 0 vs ring 3" level. The book builds the real understanding;
  it just needs a hook to hang it on.

If you are missing the assembly piece specifically, that is the one worth closing
before you continue — everything hardware-facing assumes it.

## 0.2 The toolchain, and why each piece

This project is developed on **macOS on Apple Silicon**, cross-compiling for a
bare `x86_64-elf` target. That "cross" is the important word: your build machine
is ARM and cannot even *execute* the kernel it builds — only QEMU can. The full
set (from the repo README):

```sh
xcode-select --install          # Apple clang — a cross-compiler out of the box
brew install nasm lld llvm qemu
```

What each is for, mapped to what you have already read:

- **clang** (Xcode CLT) — the C compiler. Clang is a cross-compiler natively;
  `--target=x86_64-elf` is all it takes to emit bare-metal x86-64 (Chapter 2).
  It also compiles the *pure cores* a second time, for your Mac, under sanitizers
  (Chapter 14).
- **nasm** — assembles the bootloader (flat binary, `-f bin`) and the kernel/user
  assembly stubs (`-f elf64`).
- **lld** (`ld.lld`) — the linker. Applies the higher-half linker script
  (Chapter 2). GNU `ld` is awkward to get for `x86_64-elf` on macOS; lld just
  works cross-target.
- **llvm** (brew) — provides `clang-format` and `clang-tidy` for the static gates,
  at `/opt/homebrew/opt/llvm/bin` (the `Makefile`'s `LLVM_BIN`).
- **qemu** (`qemu-system-x86_64`) — the machine you actually run on. On Apple
  Silicon this is **TCG** (software emulation, not hardware virtualization),
  because the host is ARM — slower than KVM on an x86 Linux box, but a completely
  faithful x86-64 machine, which is all that matters.

The single most useful thing to internalize here is the *shape* of the setup: one
compiler targeting two worlds (the bare-metal kernel and the sanitized host
tests), one emulator standing in for real hardware, and a linker script bridging
the physical load address and the virtual run address. Once that shape is in your
head, `make` stops being magic.

## 0.3 The references you will actually live in

No one writes an OS from memory. The skill that separates people who ship kernels
from people who get stuck is **knowing which document answers which question, and
being fast in it.** These are the ones this codebase is written against; bookmark
them and learn their table of contents:

- **Intel® 64 and IA-32 Architectures Software Developer's Manual (the "SDM").**
  The authority for everything the CPU does. You do not read it cover to cover;
  you *navigate* it. The volumes and chapters that matter for this book:
  - **Vol. 3A, Ch. 2** — control registers (`CR0`/`CR3`/`CR4`), `EFER`. Every bit
    the bootloader and `vmm_init` set is defined here.
  - **Vol. 3A, Ch. 4** — paging. The 4-level walk, the PTE bit meanings (P, W, US,
    NX), huge pages. This is Chapter 7's primary source.
  - **Vol. 3A, Ch. 5–6** — segmentation, the GDT, the TSS, privilege levels, and
    interrupt/exception delivery. Chapters 4 and 10.
  - **Vol. 3A, Ch. 6.15** — the exception reference: what `#GP`, `#PF`, `#UD`,
    `#DF` mean and which push error codes. You will return here every time you get
    an unexpected trap.
  - **Vol. 2** — the instruction reference, when you need the exact semantics of
    `syscall`/`sysret`, `iretq`, `swapgs`, `wrmsr`.
- **AMD64 Architecture Programmer's Manual, Vol. 2.** Often *clearer* than Intel's
  on long mode and the `SYSCALL`/`SYSRET` MSR layout (`STAR`/`LSTAR`/`SFMASK`) —
  worth having open next to Chapter 10.
- **The OSDev Wiki** (`wiki.osdev.org`). Not authoritative, but the fastest way to
  orient before you dive into the SDM: A20, the long-mode transition, PIC
  remapping, the higher-half kernel — every hard boot step has a page. Treat it as
  a map, then verify the details against the SDM.
- **System V AMD64 ABI** — the calling convention (which registers are
  caller/callee-saved), the stack alignment rule, the process-startup stack layout
  (`argc`/`argv`/`envp`/auxv). Chapters 9, 10, and 11 all depend on it.
- **The ELF-64 specification** and the **VIRTIO 1.2 specification** — the two
  external formats this kernel implements. When Chapter 11 or 12 says "implemented
  against the spec," these are the specs.

The meta-skill: when you hit something you do not understand — a fault you did not
expect, a bit you are not sure about — your first move is not to guess or to stare
at your code. It is to *open the SDM to the relevant chapter and read the
paragraph that defines the behavior*. Guessing about hardware is how you lose a
day; the answer is almost always one lookup away.

## 0.4 The inner loop in this repo

Three commands are your entire workflow. Learn what each proves.

```sh
make            # build build/disk.img (and fs.img)
make run        # boot it in QEMU with a window + serial on stdio
make check      # the gate: host sanitizer tests + boot integration + fsck
```

`make check` is the one you run constantly (Chapter 14). It is fast, and green-
before-commit is the law. `make run` boots the real image interactively so you can
watch the serial log and type at the keyboard echo loop. When you are working on a
pure core, the tightest loop is even smaller — build and run just the host tests:

```sh
make check-host     # ASan/UBSan unit tests, seconds, no QEMU
```

That is where you should spend most of your iteration time: the pure-core
discipline exists precisely so that most of your debugging happens here, on your
Mac, with a sanitizer and a stack trace, instead of on bare metal with neither.

## 0.5 The debug loop — the actual craft

Here is the part no amount of reading replaces. When the kernel misbehaves, you
have four instruments, in rough order of how often you reach for them.

### Instrument 1: serial `printf`, and loud failure

Your first and most-used debugger is `kprintf` to the serial console. `make run`
puts serial on stdio, so `kprintf("here: rsp=%#llx\n", rsp)` prints straight to
your terminal. This is not primitive — for a huge fraction of kernel bugs,
strategically placed prints that show you *what the code thought was true* are the
fastest path to the cause. The codebase is built to support this: every fatal path
is *loud* (`panic` prints `PANIC: file:line: message`; the bootloader prints
`ERR:`), and the panic path dumps registers. When the machine dies, read the panic
line first — it usually names the file and the reason.

Remember the diagnostic corollary from Chapter 14: because every fatal path is
loud, **a silent hang means something wedged before reaching a known failure
point** — often an early spin loop, a fault before the IDT is installed, or a
deadlock with interrupts off. Silence is itself a clue about *where* to look.

### Instrument 2: QEMU's own tracing

QEMU can tell you what the CPU is doing without any gdb. The two flags worth
knowing cold:

```sh
qemu-system-x86_64 <flags> -d int,cpu_reset -no-reboot -no-shutdown
```

- **`-d int`** logs every interrupt and exception the CPU takes, with the vector,
  error code, `CS:RIP`, and `CR2`. This is *the* tool for a triple-fault reboot:
  the log shows you the fault, then the fault-during-the-fault, then the reset, so
  you see the original cause instead of just a rebooting machine.
- **`-d cpu_reset`** dumps the full register state on every reset.
- **`-no-reboot -no-shutdown`** makes a triple fault *stop* instead of silently
  rebooting, so the log stays on screen.

Copy the repo's real device flags from the `Makefile`'s `QEMU_FLAGS` (the two
drives and the virtio-blk device) and add the `-d` flags. When you get a bare
`#DF` with no idea why, `-d int` is almost always the fastest answer.

### Instrument 3: the gdb stub — source-level kernel debugging

QEMU exposes a gdb remote stub, so you can set breakpoints in your kernel and
single-step it as if it were a normal program. Start QEMU frozen, waiting for a
debugger:

```sh
qemu-system-x86_64 <QEMU_FLAGS> -serial stdio -S -s
```

- **`-s`** opens the gdb stub on TCP `localhost:1234`.
- **`-S`** freezes the CPU at reset until you tell it to continue — so you can set
  breakpoints before the first instruction runs.

Then attach a debugger that understands x86-64 and the kernel's symbols. On an
Apple Silicon Mac you have two realistic choices, because the system gdb is
absent:

```sh
# Option A: gdb that can target x86-64 (brew)
brew install gdb        # provides a cross-capable gdb
gdb build/kernel.elf -ex 'target remote localhost:1234'

# Option B: lldb (ships with Xcode), which speaks the gdb-remote protocol
lldb build/kernel.elf -o 'gdb-remote localhost:1234'
```

You load `build/kernel.elf` because it carries the **debug symbols** (`-g` in
`KERNEL_CFLAGS`), so gdb/lldb can map addresses back to source lines, show you
locals, and let you `break kmain`, `break vmm_init`, `break trap_dispatch`. From
there it is ordinary debugging: breakpoints, `stepi`, `info registers`,
`x/16xb $rsp` to dump the stack, `print` a struct. Two caveats specific to bare
metal: set breakpoints by *symbol* or *physical/virtual address* and be aware of
which address space is live (before `vmm_init` the higher-half symbols are only
reachable through the boot mapping); and for the boot stages (real mode, 16-bit),
symbol info is minimal — you debug those with `-d int` and VGA/serial prints more
than with gdb.

A practical first exercise: `make`, launch with `-S -s`, attach gdb, `break kmain`,
`continue`, and single-step the bring-up sequence you read in Chapter 4 watching
each subsystem come alive. Seeing `gdt_init` → `pmm_init` → `vmm_init` execute
under your own control makes the whole kernel concrete in a way reading cannot.

### Instrument 4: the host sanitizers

For anything in a pure core, the best debugger is the one you already have on the
Mac. `make check-host` runs the cores under AddressSanitizer and UBSan; a failure
gives you a real stack trace pointing at the exact byte and the exact undefined
operation. When a bug *can* be reproduced in a host test, moving it there is
almost always worth the five minutes — you trade a bare-metal mystery for a
sanitized stack trace. This is the whole reason the pure-core pattern exists;
Instrument 4 is where it pays you back.

## 0.6 How to use this book to actually build one

Put the instruments together into a way of working:

1. **Read a chapter, then read its code** with the repo open, setting a gdb
   breakpoint in the function it describes and watching it run. Concept, then
   code, then *observed behavior* — all three, or it will not stick.
2. **Reproduce a subsystem from its tests** (Chapter 15 §2): blank the body of
   `pmm_core.c` or `sched_core.c`, keep the header and host tests, and reimplement
   until `make check-host` is green. The tests are a specification; passing them
   from scratch is how you learn which invariants are load-bearing.
3. **When you strike out on your own**, keep this loop: design the pure core first
   and test it on the host; push hardware to the edges; make every failure loud;
   and reach for the SDM the instant the hardware surprises you. The debug loop in
   §0.5 is what makes a from-scratch kernel *tractable* instead of terrifying —
   because when it breaks (and it will, constantly), you have four ways to see
   exactly what the machine did, instead of guessing.

You now have the equipment. The rest of the book is the map. Turn to Chapter 1.
