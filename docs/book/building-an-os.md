# Building an Operating System From Scratch

*A complete walkthrough of Hallucinate OS, from the boot sector to a graph filesystem.*

<div style="page-break-after: always"></div>

# Building an Operating System From Scratch

### A complete walkthrough of Hallucinate OS, from the boot sector to a graph filesystem

This book teaches you to build an operating system by reading and understanding
*this* one. Every chapter takes a real subsystem in this repository, explains
the hardware and OS theory it rests on, walks the actual code that implements
it, and then extracts the engineering discipline that made the code
professional instead of merely working.

It is written to be read in order, front to back. By the end you will
understand — concretely, not hand-wavingly — how a computer goes from the
first instruction the firmware runs to a C program executing in an isolated
address space and reading a file off a disk. You will also have absorbed a way
of *working* on systems software that is worth more than any single fact in
here.

## Who this is for

You already write C. You know what a pointer is, why a struct has padding, what
the stack does, and roughly what a system call is from the caller's side. What
you have not yet had to internalize is what it means to write code that runs
with **no operating system underneath it** — where a stray write corrupts a
page table instead of segfaulting, where "undefined behavior" means the machine
triple-faults and reboots, where you are the one who decides what a page
fault means.

So this book skips the C tutorial. It spends its words on the things that are
genuinely hard the first time you meet them: the exact register and memory
contracts the hardware imposes, the ordering constraints you cannot see in the
source, the failure modes that only appear on real hardware, and — throughout —
the engineering judgment that turns code that happens to boot into code you
would stake a production system on. The target reader is a junior systems
programmer who wants to become a great one. The gap between those two is mostly
judgment and rigor, and those are teachable if they are made explicit. That is
what this book tries to do.

## How the code is organized (and why that matters)

Two structural decisions shape almost every chapter, so learn them now:

1. **The arch split.** Everything x86_64-specific lives under
   `kernel/arch/x86_64/`. The rest of the kernel is architecture-neutral C.
   This is not tidiness for its own sake — it is what lets a future ARM port
   add a directory instead of rewriting the kernel, and it forces you to be
   honest about which of your assumptions are truly portable.

2. **The core/driver split.** The hardest-to-test logic in each subsystem —
   the frame allocator's bitmap math, the scheduler's ready queue, the ELF
   validator, the virtqueue ring bookkeeping, the filesystem format — is
   factored into a *pure* module (`_core.c`) that touches no hardware and
   allocates no memory. That pure module compiles unchanged for three targets:
   the real kernel, the host test suite (under AddressSanitizer and
   UndefinedBehaviorSanitizer), and the command-line tools. This is the single
   most important technique in the whole project, and [Chapter 1](01-mindset-and-method.md)
   is devoted to why.

## The chapters

| # | Chapter | What you'll build understanding of |
|---|---------|-------------------------------------|
| 0 | [Prerequisites, Tools, and the Debug Loop](00-prerequisites-and-debugging.md) | What to know first; the toolchain and references; how to actually debug bare metal |
| 1 | [The Mindset and the Method](01-mindset-and-method.md) | How to think about code with no safety net; the disciplines that recur everywhere |
| 2 | [The Toolchain and the Build](02-toolchain-and-build.md) | Cross-compiling a freestanding kernel; linker scripts; the disk image |
| 3 | [The Bootloader](03-bootloader.md) | Real mode, BIOS, A20, E820, the long-mode transition, loading an ELF |
| 4 | [Kernel Entry and the CPU's Tables](04-entry-and-cpu-tables.md) | The GDT, TSS, IDT, exceptions, and the higher-half kernel |
| 5 | [Interrupts and Time](05-interrupts-and-time.md) | The PIC, the PIT, the keyboard, and deferring work out of interrupt context |
| 6 | [Physical Memory](06-physical-memory.md) | Turning an E820 map into a frame allocator |
| 7 | [Virtual Memory](07-virtual-memory.md) | 4-level paging, the direct map, W^X, and the page-fault handler |
| 8 | [The Kernel Heap](08-kernel-heap.md) | A slab allocator, and why `kmalloc` is not `malloc` |
| 9 | [Threads and Scheduling](09-threads-and-scheduling.md) | Context switches, preemption, sleep/wake, and the interrupts-off lock |
| 10 | [Userspace and System Calls](10-userspace-and-syscalls.md) | Ring 3, `SYSCALL`/`SYSRET`, the trapframe, and validating user pointers |
| 11 | [ELF Loading and Processes](11-processes.md) | fork, execve, wait4, exit, and killing a process without killing the kernel |
| 12 | [Storage: PCI, virtio, and the Block Layer](12-storage.md) | Enumerating a bus, driving a paravirtual device, caching blocks |
| 13 | [graphfs: A Filesystem From First Principles](13-graphfs.md) | Copy-on-write, self-checksumming, and designing an on-disk format |
| 14 | [The VFS: One Namespace Over Many Filesystems](14-vfs.md) | Open files, fd tables, devfs, the kernel's first sleeping lock, and exec from disk |
| 15 | [Testing and Professional Discipline](15-testing-and-discipline.md) | The three-level test strategy, and how to make systems code testable |
| 16 | [How an OS Actually Gets Built](16-how-an-os-gets-built.md) | The commit ledger as a decision journal: slices, gates, hooks, and the definition of done |
| 17 | [Where to Go Next](17-where-to-go-next.md) | The road from here to a self-hosting, AI-native OS |
| A | [The Folklore Margin](appendix-a-folklore.md) | The tacit *whys* behind the code's decisions — naive alternative, real reason, failure avoided |
| B | [The Lab Book](appendix-b-lab-book.md) | Graded hands-on labs: diagnosis drills, reproduce-from-tests, extensions, comparative reading |
| C | [Bug Hunts](appendix-c-bug-hunts.md) | Three real bugs from this codebase's history, hunted end to end — plus the symptom triage table |
| D | [Architecture Overview](appendix-d-architecture.md) | The system as it exists today: source tree, boot flow, bring-up sequence |
| E | [Boot Protocol](appendix-e-boot-protocol.md) | The versioned bootloader ↔ kernel contract |
| F | [Memory Map](appendix-f-memory-map.md) | Physical and virtual address space layout |
| G | [Scheduling](appendix-g-scheduling.md) | Threads, context switch, preemption — design and invariants |
| H | [Userspace and System Calls](appendix-h-userspace.md) | The syscall ABI, ELF loading, the process model, fd tables |
| I | [Storage](appendix-i-storage.md) | PCI, virtio-blk, and the block layer |
| J | [The VFS](appendix-j-vfs.md) | Mounts, open files, path normalization, devfs, locking |
| K | [graphfs](appendix-k-graphfs.md) | The native filesystem's authoritative on-disk format |
| L | [Testing](appendix-l-testing.md) | The three-level test strategy behind `make check` |

## A note on how to read it

Do not skim the code blocks. The prose tells you *what* and *why*; the code is
*how*, and in systems programming the how is where the truth lives. When a
chapter quotes a file, open that file in the repository and read the parts
around the excerpt. When it names an invariant, try to imagine the bug that
would exist if the invariant were violated — that instinct, more than anything,
is what separates a good OS programmer from a great one.

Everything described here is real and runs. `make check` proves it on every
commit. When you finish the book, that command is where you begin your own
work.

<div style="page-break-after: always"></div>

# Chapter 0 — Prerequisites, Tools, and the Debug Loop

The other sixteen chapters teach you how this operating system works. This one is
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
  (Chapter 15).
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

### The tacit-knowledge canon (the *why behind the why*)

The specs above tell you what the hardware *does*. They do not tell you *why a
design is shaped the way it is* — why you block instead of spin, why a struct
field is padded to a cache line, why a lock word bounces between cores under
contention, why 16-byte stack alignment, why write-through over write-back. That
layer — call it mechanical sympathy — is rarely written next to the code it
explains; it lives in a small canon of texts and in the heads of people who have
been burned. Worth knowing early: this codebase is **uniprocessor, `-mno-sse`,
and emulated**, so it deliberately *cannot* provoke a large part of this layer
(false sharing, memory-ordering barriers, NUMA, real cache-miss and
branch-misprediction costs). Read these to build the instinct the code cannot
give you directly (Appendix A collects the specific *whys* this codebase's own
decisions embody):

- **Ulrich Drepper, *What Every Programmer Should Know About Memory*** (free PDF /
  LWN series). The single best source for the cache hierarchy, coherence (MESI),
  **false sharing**, and NUMA — i.e. the "cache bouncing" instinct. If you read
  one thing here, read this.
- **Agner Fog's optimization manuals** (`agner.org/optimize`, free). Especially
  *The microarchitecture of Intel, AMD and VIA CPUs* and *Calling conventions* —
  the ground truth on pipelines, instruction cost, and the ABI/codegen reality
  behind rules like 16-byte stack alignment.
- **Intel SDM Vol. 3A, Ch. 8–11** (memory ordering and caching) and the **Intel
  64 and IA-32 Optimization Reference Manual**. The authoritative version of what
  Drepper and Fog explain — memory-ordering guarantees, `mfence`/`lfence`/`sfence`,
  cache control. The chapters you reach for when correctness, not just layout,
  depends on the memory model.
- **Paul McKenney, *Is Parallel Programming Hard, And, If So, What Can You Do
  About It?*** ("perfbook", free PDF). The reference for when you reach SMP:
  memory barriers, lock contention and its cures (backoff, MCS/queued locks,
  per-CPU data), and RCU. Directly relevant the day this kernel grows a second
  CPU and the interrupts-off lock becomes a real spinlock.
- **Hennessy & Patterson, *Computer Architecture: A Quantitative Approach*.** The
  architecture bible — the memory hierarchy, pipelining, and speculation done
  *quantitatively*, so "a cache miss is expensive" becomes an actual number you
  can reason with.
- **Brendan Gregg, *Systems Performance*.** Methodology — how to *measure* rather
  than guess where the cost is, and the mental models (USE method, latency
  hierarchies) that turn folklore into evidence.
- **LWN.net, and real kernel commit messages / mailing-list threads.** Much of
  this knowledge exists *only* in changelogs and design debates. When you wonder
  "why did Linux do it this way," the answer is often one `git log` or LWN article
  away — and reading those threads teaches you the reasoning style itself.

Pair this reading with the habit from which it came: whenever you meet a design
decision and think *"why this and not the obvious alternative?"*, chase the answer
down to the hardware or ABI reason and the failure mode the naive choice would
hit. That question, asked repeatedly against code you already understand, is how
the tacit layer is actually built.

## 0.4 The inner loop in this repo

Three commands are your entire workflow. Learn what each proves.

```sh
make            # build build/disk.img (and fs.img)
make run        # boot it in QEMU with a window + serial on stdio
make check      # the gate: host sanitizer tests + boot integration + fsck
```

`make check` is the one you run constantly (Chapter 15). It is fast, and green-
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
(Appendix C shows all four in action — three real bugs from this codebase's
history hunted end to end — and closes with a symptom-to-instrument triage
table worth keeping open while you work.)

### Instrument 1: serial `printf`, and loud failure

Your first and most-used debugger is `kprintf` to the serial console. `make run`
puts serial on stdio, so `kprintf("here: rsp=%#llx\n", rsp)` prints straight to
your terminal. This is not primitive — for a huge fraction of kernel bugs,
strategically placed prints that show you *what the code thought was true* are the
fastest path to the cause. The codebase is built to support this: every fatal path
is *loud* (`panic` prints `PANIC: file:line: message`; the bootloader prints
`ERR:`), and the panic path dumps registers. When the machine dies, read the panic
line first — it usually names the file and the reason.

Remember the diagnostic corollary from Chapter 15: because every fatal path is
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
2. **Reproduce a subsystem from its tests** (Chapter 17 §2): blank the body of
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

<div style="page-break-after: always"></div>

# Chapter 1 — The Mindset and the Method

Before any code, the operating assumptions. Systems programming is not
application programming with more pointers. It is a different relationship with
the machine, and the habits that make you productive in userspace can actively
hurt you here. This chapter is the part of the book you will most want to skip
and most benefit from not skipping.

## 1.1 There is no one to catch you

In an application, a bug has a floor. Dereference null and the kernel delivers
`SIGSEGV`. Overrun a buffer and, if you are lucky, the allocator's redzone trips
or ASan prints a stack trace. Leak memory and the process exits and the OS
reclaims everything. You program on top of a machine that has already been made
safe *for* you by the very software you are now writing.

Remove that floor and the character of every mistake changes:

- A wild write does not fault; it silently corrupts whatever physical page it
  landed on. The symptom appears ten milliseconds and three subsystems later,
  as a scheduler that jumps to a garbage address.
- "Undefined behavior" in the C standard is not an abstraction here. Signed
  overflow, a misaligned access with alignment checking on, a jump through an
  uninitialized function pointer — these become real CPU exceptions, and if you
  have not yet installed a handler for them, the CPU escalates to a double
  fault and then a triple fault, which the hardware resolves by resetting.
- There is no `printf` until you write one. There is no debugger attached
  unless you have built the stub for it. For long stretches, your only output
  is whatever you can push out a serial port one byte at a time.

The correct response to this is not fear; it is **precision**. You compensate
for the missing safety net by moving the checking earlier — into design, into
invariants you can state in one sentence, and into tests that run on every
build. The rest of this chapter is the specific machinery this codebase uses to
do that.

## 1.2 The pure-core pattern (the single most important idea in the repo)

Most bugs in a kernel are not in the hardware poking — they are in the *logic*
wrapped around it: the off-by-one in a bitmap, the ready queue that loses a
thread under a particular interleaving, the ELF header field you forgot to
bounds-check, the filesystem extent math. That logic is also exactly the part
that does not *need* hardware to run. So this project splits nearly every
subsystem in two:

- A **pure core** (`kernel/mm/pmm_core.c`, `sched_core.c`, `proc_core.c`,
  `virtq_core.c`, `elf64.c`, `graphfs_core.c`, ...) that is plain C over
  abstract inputs. It touches no MMIO, takes no locks, and — critically —
  does no dynamic allocation: buffers are passed in by the caller. It is a
  state machine and some arithmetic.
- A thin **kernel driver** (`pmm.c`, `sched.c`, `process.c`, ...) that owns the
  hardware, the locking, and the allocation, and calls into the core for the
  logic.

The payoff is that the pure core compiles *unchanged* for three targets:

1. The real kernel (`--target=x86_64-elf`, freestanding).
2. The host test suite on your Mac, under
   `-fsanitize=address,undefined` — sanitizers that physically cannot run in a
   freestanding kernel build, now auditing your core logic on every `make check`.
3. The command-line tools (`graphfs_mkfs`, `graphfs_fsck`) that must, by
   construction, agree byte-for-byte with what the kernel does — because they
   run the same code.

`kernel/fs/graphfs_core.c`'s header comment states the discipline directly:
"no kernel dependencies, no dynamic allocation (block scratch lives in `struct
gfs`; the writable allocator and fsck take caller-supplied buffers). It
compiles for the kernel, the host mkfs/fsck tools, and the host test suite under
ASan/UBSan." That is not documentation written after the fact — it is a
constraint the code was designed to satisfy, and the reason a 20,000-round
randomized scheduler stress test can run against a shadow model on your laptop
(`tests/host/test_sched.c`) while the identical scheduler policy drives ring-3
processes on the metal.

**The lesson to internalize:** when you meet a hard piece of systems logic, the
first design question is not "how do I write it" but "how do I write it so it
can be tested somewhere with a safety net." Usually the answer is: make it a
pure function of its inputs and push the hardware to the edges.

## 1.3 Complete or absent

This codebase has a rule, stated in Appendix D as a design
principle: *"Features are fully implemented within their documented scope or not
merged. Unsupported operations fail explicitly."*

This is a discipline, and juniors routinely get it wrong in the opposite
direction — they leave a half-built feature returning a plausible-looking zero,
or a `// TODO: handle this` on a path that will absolutely be hit. In a kernel
that is how you get corruption. The habit to build:

- If a syscall number is not implemented, it returns `-ENOSYS`. Always. Never a
  silent zero.
- If the filesystem is asked for a file that needs more than the eight inline
  extents the v1 format supports, it returns `GFS_EFRAG` — a real error — not a
  truncated file.
- If the ELF loader is handed an image it has not fully validated, it does not
  load it. Validation is a *separate, total* function whose contract is: after
  it returns OK, every subsequent arithmetic step in the loader is provably
  in-bounds and overflow-free.

The corollary is that *documented limits are a feature*, not an admission of
failure. Appendix H has a whole "Known limits of this slice" section:
eager fork with no copy-on-write, no `WNOHANG`, no FPU save, static `ET_EXEC`
only. Each is a deliberate, bounded scope with an explicit failure for anything
outside it. That is what "professional" means in kernel work — not that
everything is done, but that the boundary between done and not-done is exact and
enforced by the code.

## 1.4 State your invariants in one sentence

Concurrency and hardware make kernel state fragile. The defense is to keep, for
each subsystem, a short list of invariants so simple you can check them by
inspection. The scheduler's list (Appendix G) is the model:

1. `schedule()` is entered with interrupts disabled — always (and it asserts
   this).
2. The running thread is never on the ready queue or the sleep list.
3. The idle thread never blocks, sleeps, exits, or enters the ready queue.
4. A thread is on at most one list at a time (`next` is its only link).
5. A zombie's stack is freed only from `thread_join`, which can only run after
   the zombie's final context switch has left that stack for good.

Every one of these is a single declarative sentence, and every one, if
violated, is a specific catastrophe (double-scheduling, use-after-free of a live
stack, a lost wakeup). Writing them down does three things: it turns "I think
this is safe" into a claim you can test, it tells the next reader what not to
break, and it converts vague fear into a checklist. When you add code, the
question becomes mechanical: does this preserve all five? `KASSERT` the ones you
can afford to check at runtime; comment the ones you cannot.

## 1.5 The one-CPU lock, and being honest about it

This kernel is currently uniprocessor, and it exploits that: the "lock" that
protects the scheduler lists, the heap, the frame allocator, and console output
is simply *disabling interrupts* (`cpu_irq_save`/`cpu_irq_restore`, which nest
correctly so an allocator can call another allocator). On one CPU, an
interrupts-off region cannot be interrupted, so it is atomic with respect to
everything else. That is a completely legitimate design — for one CPU.

What makes it *professional* rather than a trap is that the code says so, out
loud, at every site that depends on it. The syscall entry stub comments that
the kernel-stack global works because there is one CPU and "the swapgs/per-CPU
dance arrives with SMP." The scheduling doc ends its locking section with
"These sections become real spinlocks when SMP arrives." The assumption is
load-bearing, so it is documented at the load-bearing points. When you take a
shortcut that a future requirement will invalidate, you owe the codebase a
marker at exactly the spot the shortcut lives — not a vague note in a design doc
nobody reads.

## 1.6 Make the machine prove it works, every time

Nothing in this project is "tested by me running it once." Three levels of
automated checking accumulate for the life of the codebase (Chapter 15 is the
full treatment):

- **Host unit tests** under ASan/UBSan for the pure cores.
- **In-kernel self-tests** that run on every boot, exercising the real
  toolchain's codegen (`selftest.c`).
- **A headless QEMU boot** (`tests/run_qemu.py`) that asserts specific strings
  appear on the serial console in order and fails the instant `PANIC` or `ERR:`
  shows up.

`make check` runs all of it and must be green before every commit. The point is
not the tests themselves; it is the *ratchet*. A bug you fixed in Phase 2 cannot
silently come back in Phase 5, because the marker that proved it fixed is still
being asserted three phases later. Great systems programmers are not people who
write bug-free code on the first try — no one does. They are people who build
the machinery that makes a given bug impossible to reintroduce, and then never
turn it off.

## 1.7 How to read the rest of this book

Each subsystem chapter follows the same shape:

1. **The hardware or theory contract** — what the machine actually requires,
   stated precisely, because vagueness here is where kernel bugs are born.
2. **How this code satisfies it** — the real functions and files, with the
   non-obvious decisions called out.
3. **The transferable lesson** — the piece of judgment you should carry to your
   own systems work, independent of this codebase.

Keep the repository open beside you. When a chapter cites `syscall_entry.asm`,
read the whole file, not just the quoted lines. The prose is a guide to the
code; the code is the operating system. Onward to the toolchain — because
before you can run a single instruction of your own, you have to convince a
compiler built for hosted programs to produce something that can run on bare
metal.

<div style="page-break-after: always"></div>

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
  natively, which is exactly why the host tests (Chapter 15) compile the *pure*
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
(the full contract is Appendix E):

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
kernel/**.c,*.asm --clang/nasm-->  *.o  --ld.lld -T linker.ld-->  kernel.elf
                                    |
                     tools/mkimage.py assembles + patches
                                    v
                              build/disk.img                (the boot disk)

user/*.c,*.asm  --clang/nasm/lld-->  init.elf, hello.elf
                                    |
                     build/graphfs_mkfs installs at /bin (+ --dir /dev)
                                    v
                              build/fs.img                  (the filesystem disk)
```

Two disks, two pipelines: the boot disk carries the bootloader and kernel; the
filesystem disk is a graphfs image built by the project's own `graphfs_mkfs`
tool, which installs the userspace ELFs at `/bin` — the kernel loads `/bin/init`
from it at boot (Chapter 14). It was not always so: through Phase 4 the user
ELFs were *embedded into the kernel's `.rodata`* (a `user_blob.asm` using NASM's
`incbin`) so processes could run before any filesystem existed — an honest
scaffold with a clear expiry date, torn down on schedule in Phase 5c. That is
complete-or-absent in action: rather than a fake filesystem, an embedded blob
that said plainly what it was, and then left.

You now have a toolchain that produces a bootable image for a machine with no
OS. In the next chapter the firmware loads sector 0 and runs it, and we begin
the climb from 16-bit real mode to a 64-bit kernel.

<div style="page-break-after: always"></div>

# Chapter 3 — The Bootloader

This is the chapter where the machine is at its most primitive and least
forgiving. When the firmware hands control to your code, the CPU is pretending
it is 1981: 16-bit real mode, a 20-bit address space, no memory protection, no
paging, and a BIOS whose services vanish the moment you leave real mode. Your
job is to climb from there to 64-bit long mode with paging on and a kernel
loaded at a high virtual address — and to do it without a debugger, a stack you
can trust, or a `printf`. It is the purest systems programming in the book.

## 3.1 What the firmware guarantees (and nothing more)

On a legacy BIOS boot, the firmware reads the first 512 bytes of the boot disk —
the Master Boot Record — into physical address `0x7C00`, checks that its last
two bytes are `0x55 0xAA`, and far-jumps to it. That is the entire contract. You
get:

- The CPU in **16-bit real mode**. Addresses are `segment:offset`, computed as
  `segment * 16 + offset`, giving a ~1 MiB address space. There is no memory
  protection whatsoever.
- `DL` = the BIOS drive number you booted from. You will need it for every disk
  read.
- 512 bytes of code, minus the 2-byte signature, to work with. That is not
  enough to do anything real, which is why boot is *two stages*: the MBR's only
  job is to load a bigger second stage.
- The BIOS interrupt services (`INT 0x10` video, `INT 0x13` disk, `INT 0x15`
  memory map) — available *only* in real mode.

Everything else — the stack, the segment registers' contents, whether the A20
line is enabled — is unspecified. Professional boot code assumes nothing it was
not handed.

## 3.2 Stage 1: the 512-byte handoff

`boot/stage1.asm` does exactly one thing well: load stage 2 and jump to it. Read
its opening:

```asm
start:
    ; Normalize segments and stack; some BIOSes jump here with CS=07C0.
    cli
    jmp 0x0000:.canon
.canon:
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti
    cld
    mov [boot_drive], dl
```

Every line defends against an unstated assumption. Some BIOSes enter with
`CS=0x07C0, IP=0x0000` and others with `CS=0x0000, IP=0x7C00` — the same
physical address, different segmentation. The far `jmp 0x0000:.canon`
**canonicalizes** `CS:IP` so the rest of the code knows its own addressing. The
segment registers are then zeroed so `[boot_drive]` and every other absolute
reference resolves correctly, and the stack is planted at `0x7C00` growing down
(into the space below the code, which is free). `cld` fixes the string-op
direction flag, which the BIOS leaves in an unknown state. Only *then* does it
save `DL`. This is defensive coding at the lowest level: before you use a
resource, put it in a known state, because the layer below you promised nothing.

Next, stage 1 refuses to guess about the disk:

```asm
    mov ah, 0x41
    mov bx, 0x55AA
    int 0x13            ; INT 13h extensions present?
    jc  .err_no_ext
    cmp bx, 0xAA55
    jne .err_no_ext
```

It checks for INT 13h *extensions*, which provide LBA (linear block address)
reads via a Disk Address Packet. The old CHS (cylinder-head-sector) geometry is
deliberately **not** supported — a documented minimum-platform requirement, true
on everything since the mid-90s and always in QEMU. This is complete-or-absent
again: rather than a fragile CHS fallback that would rot untested, an explicit
capability check and a clear error. The read itself retries three times with a
controller reset between attempts, because real drives fail transiently right
after power-on — a robustness detail that only matters on the real hardware this
project intends to eventually boot on, written in now so it is not a scramble
later.

Finally, stage 1 verifies stage 2 actually loaded (its first dword is the magic
`"HLS2"`) before jumping to it. It does not trust the disk read to have done
what it asked; it checks. And every failure path prints a message containing
`ERR:` and halts — the string the test harness keys on to fail a boot
immediately instead of hanging. **Make your failures loud and machine-detectable.**

## 3.3 Stage 2: the climb, in seven moves

Stage 2 (`boot/stage2.asm`, ~530 lines) has room to do real work. It executes a
precise sequence, each step a prerequisite for the next. The full contract is
Appendix E; here is what each move *is* and why it is hard.

**1. Enable the A20 line.** For backwards compatibility with the 1 MiB
wraparound behavior of the original 8086, PCs boot with address line 20 forced
low, so physical addresses above 1 MiB alias down. You cannot use memory above
1 MiB until A20 is on. Stage 2 tests for wraparound (writes differ at `0x0500`
and `0x100500`), and if disabled tries three methods in order — the BIOS
(`INT 15h AX=2401h`), then the "fast A20" port `0x92`, then the ancient 8042
keyboard controller. Every keyboard-controller poll is bounded (64K iterations)
so a dead controller cannot hang the machine forever. The lesson in that
bounding: **in boot code, every wait must have a ceiling**, because there is no
watchdog above you to break a spin loop.

**2. Get the memory map (E820).** The kernel needs to know which physical
address ranges are actual RAM versus reserved/MMIO/ACPI. The only portable way
to learn this is `INT 15h, AX=E820h`, which the BIOS answers one entry at a
time. Stage 2 walks it, storing up to 64 raw 24-byte entries directly into the
bootinfo block the kernel will read. A subtle correctness detail: it presets the
ACPI extended-attributes dword to 1 before each call, so that BIOSes which
return only the older 20-byte entry still leave a valid attribute field. Getting
this wrong gives you a memory map that looks fine and occasionally isn't — the
worst kind of bug.

**3. Load the kernel with unreal mode.** The kernel ELF must land at physical
16 MiB, but real mode can only address ~1 MiB. The trick is **unreal mode**:
briefly enter protected mode to load a segment descriptor with a 4 GiB limit,
then drop back to real mode. The segment's cached limit stays 4 GiB, so
real-mode code can now use 32-bit offsets (`a32 rep movsd`) to touch high
memory while still calling BIOS disk services. Stage 2 reads the kernel in 32
KiB chunks to a low buffer, then copies each chunk high. Critically, it
re-enters unreal mode after *every* INT 13h call, because the BIOS may reset the
cached descriptor limits behind its back — exactly the kind of "the layer below
me silently changed my state" hazard that defines this environment.

**4. Enter 32-bit protected mode.** Load a Global Descriptor Table (null, 32-bit
code, data, and a 64-bit code descriptor prepared for the next step), set
`CR0.PE`, and far-jump to flush the pipeline into 32-bit code.

**5. Build page tables.** Long mode *requires* paging — you cannot enter it with
paging off. Stage 2 hand-builds a minimal 4-level table at `0x70000` mapping the
first 1 GiB of physical memory twice with 2 MiB pages: an **identity** map
(`PML4[0]`, so virtual == physical, needed for the instruction pointer right
after the switch) and a **higher-half** map at `0xffffffff80000000`
(`PML4[511] → PDPT[510]`, the kernel's link address). Both point at one shared
page directory, so 1 GiB is described by a handful of tables. Chapter 7 explains
paging structures properly; here the point is that *just enough* mapping is
built to make the jump survivable, and the kernel will throw all of it away and
build real tables later.

**6. Enter long mode.** The canonical dance: set `CR4.PAE`, load `CR3` with the
PML4, set `EFER.LME` (the long-mode-enable bit in MSR `0xC0000080`), then set
`CR0.PG` to turn paging on — and the CPU transitions to long mode. A far jump
into the 64-bit code segment flushes the pipeline. Order matters absolutely
here; PAE before paging, LME before PG. Get it wrong and the CPU faults with no
handler installed.

**7. Load the ELF.** Now in 64-bit mode, stage 2 parses the staged kernel image
as ELF64 and, for each `PT_LOAD` segment, validates it (magic, 64-bit,
little-endian, `EM_X86_64`, `filesz ≤ memsz`, target `≥ 1 MiB`, ends below the
staging area, entry point in the higher half) and then copies the file bytes to
the segment's physical address and zeroes the BSS tail. It is a miniature,
paranoid ELF loader — the same job the kernel's own loader does for userspace in
Chapter 11, done here for the kernel itself.

Finally it jumps to the ELF entry point with `RDI` holding the physical address
of the bootinfo block — the first argument, per the System V ABI, to the C
function `kmain` will become.

One more detail that captures the environment's cruelty: **once you leave real
mode, the BIOS is gone**, so stage 2's post-protected-mode error paths cannot
call `INT 0x10` to print. They write white-on-red bytes directly into the VGA
text buffer at physical `0xB8000` instead. When you climb past a layer's
services, you must bring your own replacement for everything it did for you,
including how you scream when you fail.

## 3.4 The boot protocol as a versioned contract

Notice what the last step actually is: stage 2 fills a `struct bootinfo` at a
fixed physical address (`0x6000`) and passes its pointer to the kernel. That
struct — magic `"HLCN"`, a version, the boot drive, and the E820 array — is a
**versioned interface** (Appendix E, `BOOTINFO_VERSION`). The
kernel validates all of it on entry (`bootinfo_get()` in `main.c`) and panics on
any mismatch: bad magic, wrong version, zero or too-many E820 entries.

This is the professional move that turns two piles of assembly and C into
*components*. The bootloader and kernel are developed and can fail
independently, but they meet at a single documented, versioned, runtime-checked
contract. Any incompatible change bumps the version in the header and the doc in
the same commit. When you build a boundary between two pieces of low-level code
— and a kernel is nothing but such boundaries — give it a magic number, a
version, and a validator on the receiving side. It is three lines of code and it
converts a class of silent-corruption bugs into a loud, early panic.

## 3.5 The transferable lessons

- **Assume nothing the layer below you did not promise in writing.** Stage 1
  canonicalizes `CS:IP`, zeroes segments, and fixes the direction flag before
  doing anything, because the BIOS guaranteed none of them.
- **Every wait has a ceiling.** There is no watchdog beneath the bootloader; an
  unbounded spin is a hang with no recovery.
- **Bring your own everything after you leave a layer.** Past protected mode,
  no BIOS: your own page tables, your own screen output, your own ELF loader.
- **Boundaries get magic numbers, versions, and validators.** The bootinfo
  block is the template for every inter-component contract in the system.

Next, the kernel's C code takes over — and the very first thing it does is
throw away the CPU tables the bootloader set up and install its own.

<div style="page-break-after: always"></div>

# Chapter 4 — Kernel Entry and the CPU's Tables

The bootloader jumps to `_start` in 64-bit long mode, and now C code — your code
— owns the machine. But the CPU is still using the bootloader's throwaway
descriptor tables, has no interrupt handlers, and could not survive a single
exception. This chapter is about the CPU-level furniture every x86_64 kernel
must build before it can do anything else: a stack, a GDT, a TSS, and an IDT.
These are the tables the *hardware itself* reads, and getting their formats
exactly right is non-negotiable.

## 4.1 The entry stub: the smallest possible bridge

`kernel/arch/x86_64/entry.asm` is the entire assembly boundary between the
bootloader and C:

```asm
_start:
    cli
    cld
    lea rsp, [rel kstack_top]
    xor ebp, ebp
    call kmain
.hang:
    cli
    hlt
    jmp .hang

section .bss
align 16
kstack:
    resb 16384                       ; 16 KiB boot stack
global kstack_top
kstack_top:
```

It does four things and stops. Interrupts off (there is no IDT yet, so an
interrupt would be fatal). Direction flag cleared. **A stack installed** — the
boot protocol explicitly guarantees no usable stack, so the kernel provides its
own 16 KiB in `.bss`. `rbp` zeroed to terminate stack-unwind chains at the root.
Then `call kmain`, which never returns; the `hlt` loop is a backstop for the
impossible.

Two details reward attention. The stack lives in `.bss`, which the bootloader's
ELF loader zeroed, so it starts clean. And `kstack_top` is `global` with a
comment — "the scheduler adopts this as thread 0's stack" — because Chapter 9's
scheduler will treat this boot stack as the stack of the very first thread. The
smallest file in the arch directory already anticipates a subsystem three
chapters away. That is what it looks like when the boundaries are designed
rather than accreted.

## 4.2 kmain: bring-up as a total ordering

`kmain` (`kernel/main.c`) is the spine of the whole kernel — the exact order in
which subsystems come alive. The ordering is not stylistic; it is a dependency
graph, and every edge is real:

```c
void kmain(uint64_t bootinfo_phys) {
    console_init();                 // 1. output first — you must be able to see failures
    gdt_init(); idt_init(); pic_init(); irq_init();  // 2. survive exceptions
    const struct bootinfo *bi = bootinfo_get(bootinfo_phys);  // 3. validate the contract
    pmm_init(bi);                   // 4. physical frames  (needs the E820 map)
    vmm_init(bi);                   // 5. real page tables  (needs frames to build them)
    kmalloc_init();                 // 6. heap  (needs virtual memory)
    sched_init();                   // 7. threads  (needs a heap for stacks)
    syscall_init();                 // 8. SYSCALL/SYSRET MSRs
    timer_init(100); keyboard_init(); cpu_enable_interrupts();  // 9. now safe to take IRQs
    pci_init(); virtio_blk_init(); block_selftest();  // 10. storage
    selftest_run();                 // 11. prove the lower half works
    process_run_init();             // 12. ring 3
    kprintf("boot: complete\n");
}
```

The first non-obvious choice: **`console_init()` is first**, before even the CPU
tables, so that if anything after it panics you can see the message. You bring up
your eyes before you bring up anything you might need to watch. The second:
interrupts are enabled (step 9) only *after* the IDT, PIC, timer, and keyboard
are all ready — turn them on any earlier and the first stray IRQ jumps through an
uninitialized vector. The third: memory management comes up in strict layers —
physical frames, then virtual mapping built out of those frames, then a heap
carved from that virtual space, then thread stacks from the heap. You cannot
reorder any of it. When you design a bring-up sequence, write it as a dependency
list first; the code order falls out of it.

## 4.3 Building your voice: serial, VGA, kprintf, and panic

Chapter 1 warned you: "There is no `printf` until you write one." This is
where it gets written — and it deserves more than the one line most books
give it, because the console stack is the instrument you will debug *every
other subsystem* through. Code you use to find bugs must be held to a higher
standard than the code you find them in: it has to work when the rest of the
kernel is broken, and it must never make things worse. Every design choice
below follows from that.

As the commit ledger records (Chapter 16), `vsnprintf` was written and
host-tested *before the bootloader existed* — the first freestanding code in
the project ran under AddressSanitizer on a Mac before any code ran on the
metal. `kernel/lib/fmt.c` is a complete C99 formatter (flags, width,
precision, all the length modifiers) built as a pure core: it renders into a
caller-supplied bounded buffer through a tiny `sink` that counts the full
would-be length while never writing past capacity. No allocation, no
recursion, no floating point — nothing that could fail in a way the caller
has to handle, because this code runs inside `panic()`, where there is no
one left to handle anything. Note its posture toward its own bugs: an
unknown conversion is emitted verbatim, "so bugs are visible in output."
Even the formatter fails loud.

Above it sits a two-layer output path, and the layering is deliberate:

- **`console.c`** fans each character out to both sinks — serial and VGA —
  translating `\n` to `\r\n` for serial so the transcript reads correctly in
  any terminal. Two sinks because they fail differently: VGA text mode is
  always there on a BIOS boot but invisible to automation; serial is what
  the test harness and your terminal actually read, but may legitimately be
  absent.
- **`kprintf.c`** formats into a 512-byte stack buffer, then emits the
  whole line inside one interrupts-off section. The split matters: only the
  *emission* needs atomicity (so two threads' lines cannot interleave
  mid-line once the scheduler exists), so only the emission is inside the
  critical section — formatting stays outside. Hold the lock for exactly
  the operation that needs it. And when a message exceeds the buffer, it is
  truncated *and marked* — `[kprintf: truncated]` — never silently clipped:
  a diagnostic channel that silently drops bytes is a channel you can no
  longer trust while debugging.

The serial driver itself (`drivers/serial.c`, a 16550 UART) shows the
instrument-grade standard in twenty lines. At init it runs a **loopback
self-test** — puts the UART in loopback mode, sends `0xAE`, and requires
that exact byte back before trusting the port; if the test fails, the driver
marks itself absent and every later write becomes a no-op. And its transmit
wait is **bounded**: if the transmitter never drains within a spin budget,
the driver disables itself rather than spin forever —

```c
    /* Bounded wait so a wedged UART degrades output instead of hanging boot. */
    for (uint32_t spin = 0; spin < 100000; spin++) {
        if (inb(COM1 + REG_LSR) & LSR_THRE) {
            outb(COM1 + REG_DATA, (uint8_t)c);
            return;
        }
    }
    serial_ok = false; /* transmitter never drained; stop trying */
```

Sit with why this matters: an instrument must never hang the patient. A
`kprintf` that can wedge the boot converts every future debugging session
into a game of "is it my bug or my print?" The rule generalizes to every
diagnostic path you will ever write — bounded waits, self-checks before
trust, graceful self-disabling — because the one thing an instrument cannot
do is report its own failure.

`panic()` completes the voice. It is deliberately tiny: print
`PANIC: file:line:` (the macro captures the call site), format the message,
print `system halted`, halt forever. File and line cost nothing and convert
"the kernel died" into "the kernel died *here*, because *this*" — which,
per Chapter 0, is the first thing you read in any hunt. The register dumps
for hardware exceptions live one layer down, in the trap dispatcher (§4.5),
where the trapframe actually is. Together they enforce the property the
whole test architecture leans on (Chapter 15): every fatal path is loud,
patterned, and machine-detectable — which is precisely what makes silence
itself diagnostic.

## 4.4 The GDT and TSS: segmentation's vestige, and the double-fault stack

In long mode, segmentation is *mostly* dead — the flat 64-bit address space
means code and data segments have base 0 and no limit. But the Global Descriptor
Table has not disappeared, because two things still need it:

1. **Privilege levels.** The CPU determines ring (0 = kernel, 3 = user) from the
   code segment selector's low bits. You need a ring-0 code/data pair and a
   ring-3 code/data pair. `SYSCALL`/`SYSRET` (Chapter 10) also derive their
   selectors from the GDT layout by fixed offset arithmetic, which is why
   `gdt.h` lays the user descriptors out in a specific order — a Phase 2
   decision made to serve a Phase 4 feature.

2. **The Task State Segment.** The one field of the TSS that matters in long
   mode is `rsp0`: the stack pointer the CPU loads automatically when an
   interrupt or trap crosses from ring 3 to ring 0. Without it, an interrupt
   taken while userspace is running would try to push the interrupt frame onto
   the *user* stack — a security hole and a reliability disaster. The scheduler
   updates `TSS.rsp0` on every context switch to point at the incoming thread's
   kernel stack (Chapter 10).

The TSS also carries the **Interrupt Stack Table**, and this codebase uses one
IST slot for a specific, sharp reason: the double-fault handler runs on a
*dedicated* stack. A double fault (`#DF`) is what the CPU raises when it faults
while trying to deliver another fault — for example, a page fault whose handler
itself page-faults because the kernel stack overflowed into an unmapped guard
page. If `#DF` tried to use the same broken stack, it would fault again and
escalate to a triple fault, which resets the machine. Giving `#DF` its own known-
good IST stack means even a kernel-stack-overflow produces a diagnostic instead
of a silent reboot. That is a deliberate investment in *debuggability of the
worst case* — exactly where beginners under-invest.

## 4.5 The IDT: 256 vectors and one dispatcher

The Interrupt Descriptor Table maps each of the 256 interrupt/exception vectors
to a handler. The CPU uses vectors 0–31 for architectural exceptions (0 = divide
error, 6 = invalid opcode, 13 = general protection, 14 = page fault, ...), and
0x20 upward will be the hardware IRQs after the PIC is remapped (Chapter 5).

Writing 256 near-identical assembly stubs by hand is exactly the kind of tedium
that breeds copy-paste bugs, so `isr.asm` generates them with a macro. Each stub
pushes the vector number (and a dummy error code for the exceptions that do not
push one, so the stack frame is *uniform* across all vectors — a small
normalization that makes the C side vastly simpler), then jumps to a common path
that saves registers and calls one C dispatcher, `trap_dispatch` (`trap.c`).
Uniform frames from non-uniform hardware is a classic and worth copying: absorb
the irregularity at the lowest layer so everything above it sees one shape.

The dispatcher's policy is where the design shows. For an *unhandled* exception
in kernel mode, it dumps every register and panics — a kernel exception is a
kernel bug by definition, and you want the full state at the moment of death.
But `trap_dispatch` first checks the saved code segment's privilege bits (the
RPL), and this is the crucial fault-isolation decision the whole userspace model
rests on:

> A hardware exception raised in **ring 3** is never the kernel's problem: the
> dispatcher logs one line and kills the offending process with the matching
> Linux signal, while the kernel and every other process keep running. Only
> ring-0 faults — and machine-level events like NMI, `#DF`, `#MC` — still panic.

We will not have processes to kill until Chapter 11, but the *mechanism* — "look
at who was running when the fault hit, and respond differently for kernel vs.
user" — is built into the trap dispatcher from the start. The right place to
decide whether a fault is fatal is the single choke point every fault flows
through, and that place exists precisely because the IDT funnels all 256 vectors
into one dispatcher.

## 4.6 The transferable lessons

- **Bring up your output before anything that might fail.** You cannot debug
  what you cannot see, and the first subsystem to break is often an early one.
- **Hold your instruments to a higher standard than the code they debug.**
  Self-test before trusting (the UART loopback), bound every wait, degrade
  loudly instead of hanging, and never silently drop diagnostic bytes.
- **Absorb hardware irregularity at the lowest layer.** Uniform interrupt
  frames, synthesized from vectors that do and do not push error codes, make
  every layer above simpler and less bug-prone.
- **Invest in the worst case.** The dedicated double-fault stack costs one IST
  entry and turns a silent triple-fault reboot into a readable panic. The
  cheapest time to build worst-case diagnostics is before you need them.
- **Funnel decisions through one choke point.** Kernel-vs-user fatality is
  decided in the single dispatcher all faults reach, which is why the policy is
  one `if` instead of scattered special-casing.

The CPU can now survive an exception and knows its privilege levels. Next it
needs to handle the *asynchronous* interrupts — the timer and the keyboard —
that turn a program that runs top-to-bottom into a system that responds to the
world.

<div style="page-break-after: always"></div>

# Chapter 5 — Interrupts and Time

An exception is *synchronous* — it is caused by the instruction currently
executing (a divide by zero, a bad memory access). A hardware interrupt is
*asynchronous* — the timer or the keyboard raises a line whenever it feels like
it, with no relationship to what the CPU is doing. Handling asynchrony correctly
is where a program becomes a system, and it is also where a whole new category of
bugs — races, lost wakeups, reentrancy — is born. This chapter covers the
legacy 8259 PIC, the 8254 timer that gives the kernel its heartbeat, the
keyboard, and the discipline of doing as little as possible in interrupt context.

## 5.1 The PIC, and why you remap it before you dare enable it

The 8259 Programmable Interrupt Controller is the legacy chip (a pair of them,
cascaded) that multiplexes hardware IRQ lines onto CPU interrupt vectors. There
is exactly one thing you must know before turning interrupts on: by default the
PIC delivers IRQs 0–15 on vectors **0x08–0x0F and 0x70–0x77**, and vectors
0x08–0x0F *collide with CPU exceptions* (0x08 is `#DF`, the double fault; 0x0D is
`#GP`). If you enable interrupts without remapping, the first timer tick arrives
as vector 0x08 and the kernel processes a hardware timer interrupt as a double
fault. This is a rite-of-passage bug for OS beginners.

So `pic_init()` remaps the two PICs to vectors **0x20–0x2F**, safely above the
32 architectural exception vectors, and then **masks everything**. Bring-up
order matters: the PIC is remapped and fully masked in step 2 of `kmain`, long
before interrupts are globally enabled in step 9. You unmask a line only when its
handler is installed and its device initialized. The general rule — turn a source
off until its consumer is ready — is why `cpu_enable_interrupts()` sits after
`timer_init()` and `keyboard_init()` and not before.

Every interrupt handler must also send an **End-Of-Interrupt** to the PIC, or
that line never fires again. Forgetting the EOI gives you a device that works
exactly once. The IRQ layer (`irq.c`) centralizes this so individual handlers
cannot forget.

## 5.2 The timer: the kernel's sense of time and its preemption engine

The 8254 PIT is programmed by `timer_init(100)` to fire IRQ 0 at **100 Hz** — one
tick every 10 ms. That single periodic interrupt does double duty:

1. It is the kernel's **clock**. `timer_ticks()` counts ticks; `timer_sleep_ticks()`
   blocks for a number of them. The boot sequence proves the timer actually
   advances by sleeping three ticks and checking the counter moved — a liveness
   assertion, not just an initialization.
2. It is the **preemption source**. The tick handler calls the scheduler's tick
   hook, which is what lets the kernel take the CPU away from a thread that would
   otherwise run forever. Without a timer interrupt there is no preemption, only
   cooperative yielding — the difference between a real multitasking kernel and a
   coroutine library. Chapter 9 details how the tick drives round-robin
   scheduling.

The design choice worth noting is *decoupling*: the timer driver does not know
the scheduler exists. It exposes `timer_set_tick_hook()`, and the scheduler
registers itself. The timer's job is "call this function 100 times a second"; the
policy of what that does lives entirely in the scheduler. This keeps the driver
reusable and testable and keeps a hard dependency from pointing the wrong way (a
device driver should never depend on a policy layer above it).

## 5.3 The keyboard, and the cardinal rule of interrupt handlers

The PS/2 keyboard raises IRQ 1 with a *scancode* — a hardware key-event code, not
an ASCII character. Translating scancodes to characters involves state (shift,
caps lock) and a lookup table, and that translation is pure logic with no
hardware in it. So, following the core/driver split from Chapter 1, the scancode
map is factored into `kernel/drivers/kbd_map.c` — a pure function, host-tested in
`tests/host/test_kbd.c`. The interrupt handler reads the scancode from the
hardware port; the pure code turns it into a character.

This points at the cardinal rule of interrupt handlers: **do the minimum in
interrupt context, defer the rest.** An interrupt handler runs with the
interrupted thread's work suspended and (often) further interrupts masked; time
spent in it is time stolen from everything else, and any state it touches races
with the code it interrupted. So the handler should read the hardware, stash the
raw event somewhere, wake whoever cares, send the EOI, and get out. The
character translation, the echo, the line editing — that is the consumer's job,
run later in normal context. `kmain`'s final loop is the consumer: it calls
`keyboard_getchar()`, and if there is nothing, `cpu_wait_for_interrupt()`
(`hlt`) until the next interrupt. Producer in interrupt context, consumer in
thread context, a buffer between them — the shape of nearly every driver you will
ever write.

## 5.4 Reentrancy and the interrupts-off lock

Asynchrony creates a problem that did not exist before: code can now be
interrupted *in the middle of touching shared state*. If the timer fires while
`kmalloc` is halfway through unlinking a slab free-list node, and the interrupt
handler (or a thread it wakes) calls `kmalloc`, the free list is corrupt.

On a uniprocessor, the fix is blunt and correct: the shared-state critical
sections run with interrupts disabled. As Chapter 1 introduced, disabling
interrupts *is* the kernel's lock here, because on one CPU nothing else can run
while interrupts are off. `cpu_irq_save()`/`cpu_irq_restore()` bracket these
regions and — importantly — **nest**: they save and restore the previous
interrupt state rather than unconditionally enabling, so an allocator that calls
another allocator, each taking "the lock," does not prematurely re-enable
interrupts on the inner unlock. Getting nesting right is the difference between a
lock that composes and one that opens a one-instruction window every time it is
used reentrantly.

There is a matching subtlety in how the interrupt path interacts with the
scheduler. When a tick or a wakeup decides a reschedule is needed, the actual
context switch happens **at interrupt exit, after the EOI** — not in the middle
of the handler — so the in-service interrupt is fully retired before another
thread runs. Chapter 9 covers this; flag it here as the reason the timer handler
does not simply call `schedule()` inline.

## 5.5 The transferable lessons

- **Never enable a source before its consumer exists.** Remap and mask the PIC
  first; unmask a line only when its handler and device are ready; enable
  interrupts globally only after everything that fires is wired up.
- **Interrupt handlers do the minimum and defer.** Read the hardware, stash the
  event, wake the waiter, EOI, return. All real work happens in thread context.
- **Point dependencies downward.** The timer driver knows nothing of the
  scheduler; the scheduler registers a hook. Drivers must not depend on the
  policy layers above them.
- **Reentrant locks must nest.** Save-and-restore, never unconditional-enable,
  or reentrant use punches a hole in your critical section.

The kernel can now keep time and respond to the world. But every subsystem from
here up — the heap, thread stacks, process address spaces — needs *memory*, and
so far the kernel has been squatting on whatever physical addresses the
bootloader happened to leave free. The next two chapters build real memory
management: first the physical frame allocator, then virtual memory.

<div style="page-break-after: always"></div>

# Chapter 6 — Physical Memory

Every allocation the kernel will ever make — page tables, heap slabs, thread
stacks, process pages, filesystem buffers — ultimately comes from *physical
RAM*, measured in 4 KiB frames. Before any of that, something has to own the
question "which physical frames are free?" That something is the physical memory
manager (PMM), and it is the first place the core/driver split earns its keep on
memory itself.

## 6.1 From a BIOS memory map to a claim on RAM

The PMM's input is the E820 map the bootloader collected — a list of physical
ranges tagged usable, reserved, ACPI-reclaimable, ACPI-NVS, or bad. Only the
*usable* ranges are RAM the kernel may touch, and even those are not entirely
free: some of that RAM currently holds the kernel image, the bootinfo block, and
low-memory boot scratch. So `pmm_init()` (`kernel/mm/pmm.c`) must take the raw
E820 map and subtract everything already spoken for.

The construction order is where correctness lives, and it is worth stating
precisely (`kernel/mm/pmm.c`):

1. Scan the usable E820 entries to find the highest physical address, which sizes
   the allocator.
2. Represent free/used as a **bitmap** — one bit per 4 KiB frame. A machine with
   256 MiB has 65,536 frames, so the bitmap is 8 KiB: cheap, and O(1) to test or
   set a specific frame.
3. Place that bitmap somewhere in usable memory — a chicken-and-egg step, since
   the allocator needs memory to describe memory.
4. Mark **everything** used, then walk the usable E820 ranges and mark only those
   frames free.
5. Re-mark as used the ranges that are usable RAM but not available: the kernel
   image (from the linker symbols), low memory, and the bitmap's own frames.

Step 4's "mark everything used first, then free the usable ranges" is the safe
default in disguise. If you did it the other way — mark everything free, then
carve out the reserved parts — a range you forgot to carve out becomes a frame
you hand to the heap that is actually MMIO or ACPI tables, and the corruption is
silent. Starting from "all used" means a bug makes you run out of memory (loud,
early, obvious) rather than hand out memory you do not own (silent, late,
catastrophic). **When the two failure modes are "too conservative" and "silent
corruption," always arrange for your bugs to land on the conservative side.**

## 6.2 The pure core, again

The allocator's logic — find a clear bit, set it, return the frame; clear a bit
to free — is pure bitmap arithmetic with no hardware in it. So it lives in
`kernel/mm/pmm_core.c` and is tested under ASan/UBSan in `tests/host/test_pmm.c`,
while `pmm.c` handles the parts that *are* environmental: reading E820, the
interrupts-off locking, and the awkward bootstrap of where the bitmap physically
sits.

This split pays off precisely because bitmap code is a minefield of off-by-ones —
the frame that is bit `i` of byte `i/8`, the boundary where a range does not end
on a byte edge, the "find first free" scan that must not run off the end. Those
are exactly the bugs sanitizers and exhaustive host tests catch and a
freestanding kernel cannot. The rule from Chapter 1 in concrete form: the index
math is pure and tested on your Mac; only the hardware wiring is trusted to the
kernel build.

## 6.3 The rebasing trick: an allocator that outlives its own addressing

There is a genuinely tricky lifecycle problem here that illustrates how careful
kernel bring-up has to be about *its own* addresses. The PMM is built during boot
while the only virtual mapping in effect is the bootloader's temporary 1 GiB
window (identity plus the higher-half kernel alias). The PMM's bitmap pointer is
therefore a virtual address that is only valid *under those temporary tables*.

Then `vmm_init()` (next chapter) builds the kernel's real page tables and, in
particular, moves the "direct map" of all physical RAM from the boot window's
base to the kernel's permanent HHDM base (`0xffff800000000000`). At that instant,
the virtual address the PMM was using for its bitmap **ceases to be mapped**. If
nothing accounted for this, the first post-`vmm_init` allocation would fault.

The solution is `pmm_rebase()`: at the exact moment `vmm_init()` flips the
direct-map base, it tells the PMM to re-derive its bitmap pointer from the new
base. The physical location of the bitmap never moved; only the virtual address
you reach it through changed, and the PMM recomputes that address. This is a
recurring hazard in kernels — a data structure whose *physical* home is stable
but whose *virtual* address depends on which page tables are live — and the
discipline is to make the dependency explicit and update it at the one moment the
mapping changes, rather than hope the old address stays valid. The memory-map doc
calls this out at the seam; the code performs it at the seam. Interfaces this
sharp are only safe when the sharpness is documented at the exact point it
matters.

## 6.4 What "reserved above 4 GiB" teaches about honesty

One more decision from Appendix F: reserved E820 ranges above 4 GiB —
for instance the 64-bit PCI MMIO hole — are *deliberately not mapped* by the
kernel. The kernel maps all RAM plus the first 4 GiB (to reach legacy MMIO), and
stops. Anything the hardware placed above 4 GiB that the kernel does not yet
drive is simply absent from the address space.

That is complete-or-absent applied to the address space itself. Rather than
mapping "everything, just in case" — which would mean the kernel could touch
device memory it has no driver for and no understanding of — the map contains
exactly what the kernel can currently account for. When a driver for something up
there arrives, the mapping arrives with it. The address space is a claim about
what the kernel understands, and it should never claim more than it does.

## 6.5 The transferable lessons

- **Arrange for bugs to fail conservatively.** "Mark all used, then free what is
  usable" turns a forgotten reserved range into out-of-memory instead of silent
  corruption. Choose the initialization order that makes your worst bug loud.
- **The index math is pure — test it with a safety net.** Bitmap allocators are
  off-by-one factories; the core is host-tested under sanitizers, and only the
  E820 wiring and locking is trusted to the kernel.
- **A physical structure can outlive its virtual address.** When you change
  which page tables are live, every pointer whose validity depended on the old
  tables must be re-derived at that instant. Make the dependency explicit at the
  seam.
- **Map only what you understand.** The address space should reflect what the
  kernel can account for, not everything the hardware exposes.

The PMM hands out anonymous 4 KiB frames of physical RAM. It has no idea what a
"process" or a "read-only page" is — it just tracks free versus used. Turning
those flat frames into a structured, protected virtual address space, with the
kernel safely up in the higher half and its own code un-writable, is the job of
the virtual memory manager. That is the next chapter, and it is the heart of the
kernel.

<div style="page-break-after: always"></div>

# Chapter 7 — Virtual Memory

Virtual memory is the single most important abstraction in an operating system.
It is what lets every process believe it has the whole address space to itself,
what isolates processes from each other and from the kernel, and what enforces
that code cannot be written and data cannot be executed. Everything above this
chapter — the heap, threads, processes, the whole security model — is built on
the page tables `vmm_init()` installs. This is the heart of the kernel, so slow
down.

## 7.1 Four levels of translation

On x86_64, a virtual address is translated to physical through a **4-level page
table**. The 48-bit canonical address is chopped into four 9-bit indices plus a
12-bit page offset:

```
 47                                                        0
 [ PML4 : 9 ][ PDPT : 9 ][ PD : 9 ][ PT : 9 ][ offset : 12 ]
```

`CR3` points at the top-level table (PML4). Each 9-bit index selects one of 512
entries at that level; each entry holds the physical address of the next-level
table plus permission bits, until the final level yields the physical frame. Two
levels can *terminate early* with a "huge page" bit: a PDPT entry can map a 1 GiB
page, a PD entry a 2 MiB page. The kernel uses 2 MiB pages for the bulk direct
map (fewer tables, fewer TLB entries) and 4 KiB pages for the kernel image (so
protections can be applied at fine granularity).

The permission bits on each entry are the entire enforcement mechanism of the
system, so know them cold:

- **P** (present) — unmapped if clear; touching it faults.
- **W** (writable) — clear means read-only; a write faults.
- **US** (user/supervisor) — set means ring 3 may access it; **clear means
  kernel-only**. This one bit is the wall between userspace and the kernel.
- **NX** (no-execute, in bit 63, gated by `EFER.NXE`) — set means instructions
  cannot be fetched from the page; an execution attempt faults.

A crucial semantic detail the userspace doc calls out: the walk **propagates
`PTE_US` through the intermediate levels while the leaf entry decides the
effective permission**. A page is user-accessible only if `US` is set at every
level down to and including the leaf. This is why the kernel can share the upper
half of every address space without leaking it to userspace — the kernel's leaf
entries never set `US`, so ring 3 faults on them regardless of what the process
does. Understand this and the address-space design in Chapter 10 becomes obvious.

## 7.2 The layout: higher-half kernel, direct map, and the null trap

`vmm_init()` throws away the bootloader's temporary tables and builds the
kernel's permanent address space (Appendix F):

| Virtual range | Maps to | Attributes |
|---------------|---------|------------|
| `0` .. `0x00007fffffffffff` | *unmapped* | userspace-to-be; a null deref faults |
| `HHDM_BASE = 0xffff800000000000` + paddr | all RAM + first 4 GiB (MMIO) | 2 MiB pages, NX, global |
| `KERNEL_VMA = 0xffffffff80000000` + paddr | the kernel image only | 4 KiB pages, W^X, global |

Three ideas are packed in here.

**The higher-half kernel.** The kernel lives in the top of the address space
(`0xffffffff80000000`), the lower half is left for userspace, and the two never
overlap. This is why the kernel was compiled `-mcmodel=kernel` and linked at that
address back in Chapter 2 — the code model and the linker script exist to serve
this layout. Every process will map the kernel into its own upper half (Chapter
10), so the kernel is always addressable no matter which process is running,
without any `CR3` juggling on a syscall or interrupt.

**The direct map (HHDM — high-half direct map).** All physical RAM is mapped, at
a fixed offset, into a contiguous virtual window at `HHDM_BASE`. This gives the
kernel a trivial `phys_to_virt()`: add the base. Whenever the kernel has a
physical address (a frame from the PMM, a page-table pointer) and needs to read
or write it, it does so through the HHDM. It is mapped **NX** — the direct map is
data, never code, so no page reachable through it may be executed. And it is
mapped with 2 MiB pages because mapping gigabytes at 4 KiB granularity would burn
megabytes on page tables and thrash the TLB.

**The unmapped null region.** The entire low virtual range is left unmapped for
now, so a null-pointer dereference — in the kernel or, later, in a process —
*faults* instead of quietly reading real memory. You get the safety net you were
denied in Chapter 1 back for this one specific, common bug, and you get it for
free by simply not mapping page zero. (The ELF loader in Chapter 11 also refuses
to ever map the null page, for the same reason.)

## 7.3 W^X: making a security property a checked fact

Write-xor-execute means no page is both writable and executable. It is the
mitigation that stops an attacker who achieves a memory write from turning it
into code execution. In this kernel it is realized as a collaboration across
three layers you have already seen:

1. The **linker script** emits page-aligned symbols at each section boundary
   (Chapter 2).
2. `vmm_init()` maps `_text_start.._text_end` read-execute-not-writable, rodata
   read-only-NX, and data (including BSS) writable-NX — at 4 KiB granularity so
   the boundaries are exact.
3. It enables the CPU features that make these bits mean something:
   **`EFER.NXE`** (so the NX bit is honored at all), **`CR0.WP`** (so *even
   ring-0 code* obeys the read-only bit — without WP, the supervisor can write
   read-only pages, and your W^X is decorative), and **`CR4.PGE`** (global pages,
   so the kernel's mappings survive TLB flushes on address-space switches).

`CR0.WP` deserves a beat. By default, ring 0 ignores the writable bit — the
kernel could scribble on its own `.text`. Setting WP makes the kernel hold itself
to the same rules it imposes on everyone else. A junior engineer might reason "the
kernel is trusted, why constrain it?" The answer is that *bugs* are not trusted,
and the point of W^X is to contain the consequences of a bug, which by definition
is code doing something you did not intend. The kernel constrains itself because
the kernel is where the bugs that matter most live.

And then — the professional capstone — a **boot self-test verifies the
protections actually took**. It attempts a write to a read-only page and confirms
it faults, checks that data pages are NX, and so on. A security property you
merely *configured* is a hope; a security property you *tested at runtime* is a
fact. Appendix F: protections are "verified by boot selftests." This is
the recurring highest standard of the whole codebase — do not assert that
something is safe, arrange for the machine to demonstrate it on every boot.

## 7.4 The page-fault handler: decode before you die

When a translation fails, the CPU raises `#PF` (vector 14) with an *error code*
whose bits say why (present/not-present, read/write, user/supervisor,
instruction-fetch) and with the faulting address in `CR2`. `vmm_init()` installs
a page-fault handler that **decodes the error code and `CR2` before panicking**,
so instead of "page fault" you get "write to non-present page at
`0xffffffff80000000` from ring 0" — the difference between a five-minute and a
five-hour debugging session.

Later, in the userspace phase, this same handler learns to distinguish a fault
from ring 3 (kill the process, Chapter 11) from a fault in ring 0 (kernel bug,
panic). The infrastructure to make that distinction cheap — a single handler that
already decodes the privilege bit — was built here, in Phase 2, before there was
a process to kill. Build your diagnostic and dispatch machinery at the choke
point early; the features that need it will arrive later and find it waiting.

## 7.5 The transferable lessons

- **Permissions are per-page and per-level.** `US` and the walk's propagation
  rule are what let the kernel share its mappings into every address space while
  staying unreachable from ring 3. Learn the four bits (P/W/US/NX) exactly.
- **Give yourself back the null-deref safety net.** Leaving page zero unmapped
  turns the most common pointer bug into a clean fault instead of silent memory
  access — nearly free, always worth it.
- **Constrain the kernel with the same bits you impose on users.** `CR0.WP`
  makes W^X apply to ring 0, because the bugs W^X exists to contain live in the
  kernel too.
- **A configured protection is a hope; a tested one is a fact.** The boot
  self-test that provokes a fault against a read-only page is the difference,
  and it is the standard to hold all safety properties to.

The kernel now has a structured, protected virtual address space. But everything
so far has allocated in whole 4 KiB frames. Real kernel code needs to allocate a
37-byte control block or a 200-byte node record without wasting a page each time.
That is the job of the heap.

<div style="page-break-after: always"></div>

# Chapter 8 — The Kernel Heap

The PMM allocates in 4 KiB frames; the VMM maps them. But most kernel objects are
small — a thread control block, a process table slot's worth of metadata, a
64-byte string. Handing out a whole frame for each would waste memory
catastrophically and give you no way to pack many small objects densely. The
kernel needs a `malloc`. This chapter builds `kmalloc` as a slab allocator, and —
just as importantly — explains why `kmalloc` is a *different animal* from the
`malloc` you know, subject to constraints userspace allocators never face.

## 8.1 Why not just port a userspace malloc?

A userspace `malloc` runs in a benign world: it can call `mmap`/`sbrk` to grow,
it runs in one well-defined context, and if it is slow occasionally, nobody dies.
A kernel allocator lives somewhere much harsher, and three constraints reshape
the whole design:

- **It can be called from almost anywhere, including with interrupts disabled,
  and it must be reentrant-safe.** A device interrupt might wake a thread that
  allocates; a syscall handler allocates; the scheduler indirectly allocates.
  Every one of those touches the same free lists.
- **It cannot fault or block on its slow path the way `mmap` can.** Its backing
  store is the PMM, which it calls directly for fresh frames.
- **Fragmentation is unbounded in time.** A long-running kernel that fragments
  its heap does not get to restart nightly. The allocator's structure has to keep
  fragmentation contained by construction.

These push kernels toward the **slab allocator** design (originating in Solaris),
which is what `kmalloc` implements.

## 8.2 Slabs: size classes over raw frames

The core idea: instead of one general free list of arbitrary-sized holes (which
fragments), maintain separate pools — *slabs* — each carved into fixed-size
objects of one **size class** (say 16, 32, 64, 128, ... bytes). An allocation is
rounded up to the nearest size class and served from that class's slab; a free
returns the object to its class's free list. Within a size class every object is
interchangeable, so a free slot is *always* immediately reusable by the next
request of that size — there is no "this hole is 40 bytes but I need 48"
fragmentation *within* a class. A slab itself is one or more PMM frames diced into
that class's objects, with a small amount of bookkeeping.

This structure buys several properties at once:

- **O(1) allocate and free** — pop or push the size class's free list.
- **Bounded internal fragmentation** — at most the rounding to the next size
  class, which you can tune by choosing the class spacing.
- **Locality** — objects of the same kind cluster in the same frames.

`kmalloc_init()` sets this up over PMM frames after virtual memory is live (it
needs the direct map to touch the frames it gets), which is why it is step 6 of
`kmain`, strictly after `vmm_init()`.

## 8.3 The split you now expect

By this point in the book the structure is predictable, and that predictability
is the point. The allocator's *logic* — the size-class math, the free-list
manipulation, the slab bookkeeping — is pure and lives in
`kernel/mm/heap_core.c`, host-tested under ASan/UBSan in `tests/host/test_heap.c`.
The kernel wrapper `kmalloc.c` supplies the two things the core cannot: fresh
frames from the PMM, and the interrupts-off locking that makes the free-list
operations atomic on the uniprocessor.

Sanitizers are *exactly* the right tool for an allocator's core, because an
allocator is a machine for manufacturing subtle memory bugs: a free-list link
written one byte past the object, a double-free that corrupts the list, an
object handed out while still on the list. ASan and UBSan on the host catch the
whole family. The freestanding kernel build cannot run them; the pure core can,
because it is just C over caller-supplied memory. If you take one structural
habit from this book, let it be this: **the code most likely to have subtle
memory bugs is the code you can most easily make pure and test with sanitizers —
so do.**

## 8.4 The allocator is shared state, so it takes the lock

`kmalloc` and `kfree` touch free lists that every context shares, so they run
inside `cpu_irq_save()`/`cpu_irq_restore()` — the interrupts-off lock from
Chapter 5. Two consequences follow, and both are the kind of thing that
distinguishes someone who has been burned from someone who has not:

- Because the lock nests, kernel code may allocate while already holding the
  lock (an allocator calling another allocator, or a locked subsystem allocating
  under its own critical section) without prematurely re-enabling interrupts.
  This is why nesting was non-negotiable back in Chapter 5.
- Because allocation runs with interrupts off, it must be *fast* — every cycle in
  `kmalloc` is a cycle the timer cannot tick and the keyboard cannot be serviced.
  The slab design's O(1) common path is not just an efficiency nicety; it is what
  keeps the interrupts-off window short enough that latency stays bounded. In a
  kernel, algorithmic complexity and interrupt latency are the same conversation.

## 8.5 Allocation discipline: leaks are forever, and the tests know it

A userspace leak is bounded by process lifetime. A kernel leak accumulates until
reboot, so the kernel holds itself to a much stricter standard, and — the
recurring theme — it *proves* adherence rather than trusting it. The scheduler's
self-tests assert the heap returns to its **exact pre-test object count** after
threads are created and joined; the process self-tests assert the physical frame
count is identical before and after an entire fork/exec/wait cycle
(Appendix H: "the whole fork/exec/wait cycle leaks nothing"). A single
leaked control block would fail the boot.

This is a powerful pattern you should copy into any long-lived system you build:
make "resource count returns to baseline" an *asserted invariant* around
operations that allocate and free, and check it automatically. It converts leaks
— normally invisible until they accumulate into an out-of-memory weeks later —
into an immediate, localized test failure at the commit that introduced them. The
allocator gives you the count; discipline is checking it.

## 8.6 The transferable lessons

- **A kernel allocator is not a userspace one.** It is reentrant-safe, runs with
  interrupts off, backs onto the PMM directly, and cannot afford unbounded
  fragmentation — which is what pushes the design toward slabs.
- **Slabs trade generality for O(1) and bounded fragmentation.** Fixed size
  classes make every free slot immediately reusable and every operation constant
  time — and short critical sections keep interrupt latency bounded.
- **Make the bug-prone core pure and sanitize it.** An allocator is a memory-bug
  factory; its logic is exactly the kind of pure code ASan/UBSan can audit on the
  host.
- **Assert that resources return to baseline.** Counting objects/frames before
  and after an operation turns "forever" leaks into immediate test failures.

The kernel can now allocate small objects efficiently and safely. That unlocks
the abstraction the whole rest of the system is built on: the thread. Next we
give the kernel multiple stacks, a scheduler to switch between them, and
preemption — the machinery that turns a single sequential program into a
multitasking operating system.

<div style="page-break-after: always"></div>

# Chapter 9 — Threads and Scheduling

So far the kernel is one thread of control: `kmain` runs top to bottom. A
multitasking OS needs many independent flows — kernel worker threads now, and the
kernel-side halves of userspace processes soon — that share the CPU. This chapter
builds that: what a thread *is*, how you switch between two of them, how the
timer forcibly preempts one, and the handful of invariants that keep the whole
thing from eating itself. It is the most concurrency-subtle chapter in the book,
and the payoff is that Chapter 11's processes get preemption, sleep, and join for
free.

## 9.1 A thread is a stack (and a little bookkeeping)

Strip away the abstraction and a thread is remarkably concrete: **a stack, plus
enough saved state to resume it.** Each thread here gets a 16 KiB `kmalloc`'d
stack and a `struct thread` control block holding its state, its saved stack
pointer, and a single `next` link for whatever list it is on. The boot context —
`kmain` itself, on the boot stack from `entry.asm` — is *adopted* as thread 0,
"main." It was never created; it simply gets a control block so the scheduler can
manage the flow that was already running. (Recall `entry.asm` marked
`kstack_top` global "the scheduler adopts this as thread 0's stack" — this is the
payoff.)

The states a thread moves through (Appendix G):

- **READY** — runnable, sitting on the ready queue.
- **RUNNING** — on the CPU; at most one at a time.
- **SLEEPING** — on a wake-tick-sorted sleep list, waiting for a timer deadline.
- **BLOCKED** — parked waiting for an event (`thread_join`, or the generic
  `sched_block()`/`sched_wake()` pair that `wait4` will block on).
- **ZOMBIE** — exited, its control block and stack awaiting reclamation by
  whoever joins it.

## 9.2 The context switch: the trick at the center of it all

Here is the single most illuminating piece of code in the kernel, the entire
context switch (`kernel/arch/x86_64/ctx.asm`):

```asm
; void ctx_switch(uint64_t *save_rsp, uint64_t load_rsp)
global ctx_switch
ctx_switch:
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15
    mov [rdi], rsp      ; park the outgoing thread
    mov rsp, rsi        ; adopt the incoming thread's stack
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    ret
```

Sit with how little this is. A thread that is not running is *entirely captured
by its saved `rsp`* — because everything that defines where it was is already on
its own stack. `ctx_switch` pushes the System V **callee-saved** registers
(`rbp rbx r12–r15`) onto the outgoing stack, saves that stack pointer into the
outgoing thread's control block, loads the incoming thread's stack pointer, pops
*its* callee-saved registers back, and `ret`s — into whatever the incoming thread
was doing when *it* last called `ctx_switch`. The two threads swap identities at
the `mov rsp, rsi`.

Why only the callee-saved registers? Because `ctx_switch` is an **ordinary C
function call**. The System V ABI says a function call may clobber the
caller-saved registers, so the compiler *already spilled* anything live across
the call at the call site. `ctx_switch` only needs to preserve what the ABI says
a function must preserve. This is a beautiful example of *using the ABI as a
tool*: half the register-saving work is delegated to the compiler for free,
simply because you made the switch look like a normal call. Understanding this is
a genuine level-up — it is the moment context switching stops being magic.

There are two hard constraints, both commented in the file. First, **interrupts
must be off** across the switch: a timer interrupt landing between `mov rsp, rsi`
and the `ret` would run a handler on a half-switched thread with an inconsistent
register set. Second, a **brand-new thread has no saved frame to `ret` into**, so
the scheduler hand-builds a fake one whose return address is
`thread_entry_trampoline`, with the entry function in `r12` and its argument in
`r13`:

```asm
thread_entry_trampoline:
    sti                 ; interrupts were off (scheduler invariant); turn them on
    mov rdi, r13        ; argument
    call r12            ; entry(argument); rsp is 16-aligned here
    call thread_exit    ; falling off the entry function ends the thread cleanly
```

The trampoline is placed 8 bytes below the 16-byte-aligned stack top so that
`rsp` is 16-aligned *at the call sites*, as the ABI requires — get that wrong and
the first function that uses an aligned SSE spill (or just the ABI's alignment
assumption) crashes in a way that looks unrelated to threading. And note the
`sti`: a new thread is entered through `ctx_switch`'s `ret` with interrupts off
(the invariant), so the trampoline re-enables them before running the thread
body. Every thread's first instruction has to undo the interrupts-off state the
switch guaranteed. Falling off the end of the entry function lands in
`thread_exit`, so a thread that simply returns terminates cleanly — no special
case required.

## 9.3 Preemption: taking the CPU by force

Cooperative scheduling — threads yielding voluntarily — is not enough; one
thread with an infinite loop would freeze the system. Preemption uses the 100 Hz
timer from Chapter 5. The tick handler calls the scheduler's registered hook,
which wakes any due sleepers and sets a `need_resched` flag whenever another
thread is ready. The timeslice is one tick, 10 ms.

The subtle and correct part is *when* the switch happens. The tick handler does
**not** call `schedule()` inline. Instead the IRQ dispatcher performs the switch
at interrupt exit, **after the EOI** (`sched_preempt()`), so the in-service
interrupt is fully retired before another thread runs — otherwise the PIC would
think that IRQ is still being serviced and could stall further ticks. The
preempted thread's interrupt frame simply stays parked on its own kernel stack;
when it is scheduled back in, control returns up through the interrupt path and
`iretq`s as if nothing happened. The thread never knows it was preempted. That
transparency — a thread cannot tell the difference between running straight
through and being frozen for a scheduling quantum — *is* preemptive multitasking.

The same mechanism handles wakeups from interrupt context: making a thread READY
inside a handler flags a reschedule that fires at IRQ exit, so a thread woken by
(say) a keyboard interrupt runs within the same tick rather than waiting out the
current timeslice. Low wakeup latency, for free, from the same "switch at IRQ
exit" rule.

## 9.4 The invariants, and why each one is a specific catastrophe

Concurrency bugs are the hardest to reproduce and the most expensive to ship, so
this subsystem is governed by five invariants (from Chapter 1, now with their
teeth showing). For each, name the disaster it prevents:

1. **`schedule()` is entered with interrupts disabled — always (asserted).**
   Violate it and a switch races a timer tick, running a handler on a
   half-switched thread.
2. **The running thread is never on the ready queue or the sleep list.** Violate
   it and the thread gets scheduled onto a second CPU-time slot or "woken" while
   already running — double-scheduling, instant corruption.
3. **The idle thread never blocks, sleeps, exits, or enters the ready queue.** It
   is the fallback when everyone else is blocked, and it `hlt`s. Violate it and
   the scheduler can find *nothing* to run and dereferences a null "next."
4. **A thread is on at most one list at a time** (`next` is its only link).
   Violate it and the same thread appears on two lists, and one list's operations
   corrupt the other's.
5. **A zombie's stack is freed only from `thread_join`, which can only run after
   the zombie's final `ctx_switch` has left that stack for good.** Violate it and
   you free a stack a thread is still standing on — a use-after-free of live
   execution state, the worst bug in the chapter.

Invariant 5 is worth dwelling on because it is a genuine ordering hazard. A
thread exits by becoming a zombie and switching away *one last time*. Its stack
is still in use right up to the instant of that final `ctx_switch` — the switch
itself runs on it. Only after control has left for good is the stack safe to
free, and the one place guaranteed to run after that is the *joiner*. So freeing
is the joiner's job, never the exiting thread's. Whenever a resource's last user
is the very code releasing it, you have this hazard; the resolution is always to
move the release to some other party that provably runs strictly afterward.

## 9.5 Proving it interleaves

True to form, none of this is trusted — it is demonstrated on every boot. Three
worker threads each log a tag and yield, four rounds; FIFO round-robin makes the
serial output a perfect rotation:

```
selftest: sched interleave "abcabcabcabc"
```

Any scheduling bug perturbs that exact string, and the QEMU integration test
asserts it. The suite also proves timed sleep blocks for the requested number of
ticks, that a thread spinning without yielding cannot monopolize the CPU (the
sleeping main thread gets it back *via preemption* — proving preemption works,
not just cooperation), and that join reclaims every byte. A deterministic
output string as a concurrency oracle is a lovely technique: make the correct
interleaving produce one specific, fragile artifact, and assert on it.

## 9.6 The transferable lessons

- **A suspended thread is just a saved stack pointer.** Everything else is on its
  stack; the switch is a stack swap bracketed by register saves.
- **Use the ABI as a tool.** Making the context switch a normal C call lets the
  compiler save the caller-saved registers for you — half the work, for free.
- **Preempt at interrupt exit, not inline.** Retire the interrupt first, then
  switch; the preempted thread returns through the interrupt path none the wiser.
- **When the last user of a resource is the code releasing it, hand the release
  to someone who runs strictly later.** That is why joiners free zombie stacks.
- **Turn correct concurrency into a deterministic artifact and assert on it.**
  `"abcabcabcabc"` is a scheduling oracle.

The kernel is now a real multitasking system — but everything still runs in ring
0, fully privileged, sharing one address space. The next chapter crosses the
biggest boundary in the book: down to ring 3, into isolated user address spaces,
and back up through a system call.

<div style="page-break-after: always"></div>

# Chapter 10 — Userspace and System Calls

This chapter crosses the most important boundary in the system: from ring 0,
where code can do anything, to ring 3, where a process is boxed into its own
address space and can only affect the outside world by asking the kernel. Getting
this transition *exactly* right — every register, every stack, every privilege
bit — is the difference between an isolation boundary and a security theater. The
whole point of an OS is that a buggy or malicious program cannot take down the
system or read another program's memory, and that guarantee is enforced entirely
by the mechanisms in this chapter.

## 10.1 The address space is the isolation

Chapter 7 built the kernel's page tables. A user process gets its *own* PML4,
built by `vmm_addrspace_create_user()`, and the design is elegant:

> The user PML4's **upper half aliases the kernel PML4's entries 256–511**: the
> kernel is fully mapped into every process, so interrupts, syscalls, and the
> scheduler need no `CR3` gymnastics — but it is inaccessible from ring 3,
> because no kernel mapping carries `PTE_US`.

This is the `US`-bit rule from Chapter 7 cashed in. The kernel is present in
every address space (so a syscall or interrupt from any process lands in mapped
kernel code without switching page tables), yet ring 3 faults the instant it
touches a kernel address, because the walk requires `US` at every level and the
kernel's entries never set it. One PML4 per process, upper half shared and
protected, lower half private. A process literally cannot *name* another
process's memory or the kernel's — the addresses either are not mapped in its
space or are mapped without user permission.

One invariant falls out of sharing the upper half by aliasing: **the kernel never
adds a new top-level PML4 entry after `vmm_init()`.** If it did, existing
processes — whose upper halves were aliased at creation time — would not see the
new entry, and the kernel would be partially unmapped in old address spaces. All
kernel growth happens *underneath* the PML4 slots populated at init. That is a
non-obvious constraint that the address-space design silently imposes, and the
kind of thing you must write down (the userspace doc does) because nothing in the
code stops you from violating it — it just breaks mysteriously three processes
later.

The scheduler ties it together: it tracks an address space per thread (`NULL` for
pure kernel threads), and on a context switch reloads `CR3` **only if the target
differs from what is active**. Pure kernel threads never pay a TLB flush; two
threads of the same process do not flush on a switch between them. And on every
switch it points `TSS.rsp0` and the syscall-stack global at the incoming thread's
kernel stack — the `rsp0` from Chapter 4 and the `syscall_kstack` you are about
to meet, kept current so that *whenever* the CPU crosses into the kernel, it lands
on the right stack.

## 10.2 The syscall ABI: standing on Linux's shoulders

The native system-call convention is **identical to the Linux x86_64 ABI** — same
`syscall` instruction, same `rax` for the number, same `rdi rsi rdx r10 r8 r9`
for arguments, same `-errno` return convention. This is a strategic decision, not
laziness: Phase 7's goal is to run unmodified Linux binaries, and if the native
ABI already *is* the Linux ABI, the personality layer shares one numbering, one
register convention, and one error vocabulary with native code. Choosing your
interfaces today to match where you are going tomorrow is architecture; it costs
nothing now and saves a rewrite later.

The hardware clobbers exactly two registers on `syscall`: `rcx` (which it loads
with the return `rip`) and `r11` (the saved `rflags`). The ABI promises userspace
that *every other register survives the call*. That promise is the entire reason
the entry path looks the way it does.

## 10.3 The entry path, register by register

`syscall` is fast precisely because it does *not* switch stacks or save state —
it just jumps to the address in the `LSTAR` MSR with `rcx`/`r11` set. That
minimalism is the kernel's problem to clean up. Here is the real entry stub
(`kernel/arch/x86_64/syscall_entry.asm`), which is worth reading in full because
every line defends a specific hazard:

```asm
syscall_entry:
    mov [rel saved_user_rsp], rsp
    mov rsp, [rel syscall_kstack]   ; adopt this thread's kernel stack

    ; Build a struct syscall_frame: the COMPLETE user register state.
    push qword [rel saved_user_rsp] ; frame.rsp
    push rcx                        ; frame.rip   (hardware put user rip here)
    push r11                        ; frame.rflags(hardware put user rflags here)
    push rax                        ; frame.rax:  syscall nr in, return value out
    push rdi
    push rsi
    push rdx
    push r10
    push r8
    push r9
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15

    sti                             ; syscalls may block or be preempted
    mov rdi, rsp                    ; syscall_dispatch(frame)
    cld
    call syscall_dispatch

    cli                             ; no kernel-stack-less interrupt window on the way out
    pop r15
    ... (restore all)
    pop rax                         ; return value (dispatch wrote frame.rax)
    pop r11                         ; user rflags (sysret restores)
    pop rcx                         ; user rip
    pop rsp                         ; user stack
    o64 sysret
```

Walk the hazards it closes:

- **`syscall` runs on the user stack.** The first instruction saves the user
  `rsp` and switches to the kernel stack — read from the `syscall_kstack` global
  the scheduler keeps current (§10.1). Until that switch, the kernel is standing
  on memory the user controls, so `SFMASK` cleared `IF` (interrupts masked) at
  entry: **nothing can interrupt while we are on the user stack.** The `sti`
  comes only after we are safely on the kernel stack and the frame is built.
- **It saves *every* caller-saved user register**, not just the ones the syscall
  reads. The ABI promised userspace they survive; the C dispatcher, being a
  normal C function, will freely clobber them. So they are all pushed and all
  restored. Init actually *tests this contract*: it issues a syscall with
  sentinel values in all six argument registers and verifies them afterward.
- **The save area is precisely `struct syscall_frame`** — the field layout is
  asserted with `offsetof`. This is the linchpin of the next chapter: because the
  frame is the complete user context laid out as a struct, `fork` becomes a
  *struct copy*. The assembly and the C struct are two views of the same bytes,
  and the `offsetof` assertion is what keeps them from silently drifting apart.
- **The return path disables interrupts before switching back to the user
  stack**, so there is never a window where an interrupt could fire while the CPU
  is on a user-controlled stack with kernel expectations. Symmetric with entry.

Sixteen pushes (a detail the comment flags) keep `rsp` 16-byte aligned at the
`call`, as the ABI requires — the same alignment discipline as the thread
trampoline in Chapter 9. Alignment bugs in the entry path are especially nasty
because they only bite inside the callee, far from the cause.

The MSR setup that makes this work (`usermode.c`): `EFER.SCE` enables the
`syscall` instruction, `LSTAR` holds the entry stub's address, `SFMASK` says
which flag bits to clear on entry (`IF|TF|DF|AC`), and `STAR` holds the segment
selectors. That last one is why `gdt.h` laid out the user descriptors in a
specific order back in Chapter 4 — `sysret` computes the user selectors from
`STAR` by fixed `+8/+16` arithmetic, so the GDT had to be arranged to feed it.
A Phase 4 instruction constrains a Phase 2 table; the layout was chosen in
advance to serve it.

## 10.4 One entry primitive for every drop into ring 3

Descending *into* ring 3 — whether launching a brand-new process or resuming
`fork`'s child — is a single primitive, `user_frame_enter(frame)`, an `iretq`
that loads the frame's complete register state atomically with the ring
transition:

```asm
user_frame_enter:
    cli
    push qword 0x23             ; ss  (user data selector | RPL 3)
    push qword [rdi + 15*8]     ; frame.rsp
    push qword [rdi + 13*8]     ; frame.rflags (IF set)
    push qword 0x2B             ; cs  (user code selector | RPL 3)
    push qword [rdi + 14*8]     ; frame.rip
    ... load every GPR from the frame ...
    iretq
```

A new process's first entry is just a *zeroed* frame with `rip`/`rsp`/`rflags`
set — so nothing of the kernel's register state leaks into ring 3 — and `iretq`
drops to the entry point. `fork`'s child is the parent's saved frame with `rax`
set to 0. The *same instruction sequence* handles both, because both are
"materialize this exact user context and go." Unifying process launch and fork
resumption into one primitive is what makes the process model in Chapter 11 so
small. When two operations are secretly the same operation, finding the shared
primitive collapses a lot of code and a lot of bugs.

## 10.5 Never trust a pointer from ring 3

A user program passes the kernel pointers — the buffer for `write`, the path for
`execve`. Those pointers are *adversarial input*: they might point at kernel
memory, at unmapped pages, at another process's data, or be arithmetic that
overflows. The kernel validates **every** user pointer before touching it
(`uaccess.c`), and the rule is exact:

> The range must lie entirely below `USER_VA_LIMIT` (`0x0000800000000000`, the
> canonical lower half), and every page it spans must be **present and
> user-accessible (`PTE_US`)** in the *caller's* address space. Anything else is
> `-EFAULT`, and nothing is touched.

Two things make this correct rather than approximate. It checks the *caller's*
address space, not "some" address space — the pointer's validity is
per-process. And it checks *every page* the range spans, not just the first byte
— a buffer can start in a mapped page and run off into an unmapped one, and
checking only the start is a classic exploitable bug. The kernel checks the whole
range before the first access, so a malicious length cannot walk it off the end
of valid memory. This is the software half of isolation; paging permissions are
the hardware half. (`SMEP`/`SMAP`, which let the CPU trap kernel access to user
pages, are noted as later additions; until then this validation plus the page
bits are the guarantee.)

## 10.6 The transferable lessons

- **Isolation is the address space.** Per-process page tables with a shared,
  `US`-cleared kernel upper half give you "kernel always mapped, never reachable"
  with no `CR3` tricks — and quietly forbid adding top-level entries post-init.
- **Choose interfaces for where you are going.** Adopting the Linux syscall ABI
  natively costs nothing now and makes the future compatibility layer share one
  vocabulary with native code.
- **Close every window on the boundary.** Interrupts stay masked whenever the CPU
  stands on a user-controlled stack; the full caller-saved set is preserved
  because the ABI promised it; entry and exit are exactly symmetric.
- **Find the shared primitive.** Process launch and fork resumption are one
  `iretq` over a frame, because both are "become this user context."
- **User pointers are adversarial.** Validate the whole range, in the caller's
  space, for presence and user-accessibility, before the first byte — or return
  `-EFAULT` and touch nothing.

The plumbing to run ring-3 code and service its syscalls now exists. The next
chapter uses it to build the Unix process model — loading an ELF, forking,
exec'ing, waiting — and to make a crashing process die alone.

<div style="page-break-after: always"></div>

# Chapter 11 — ELF Loading and Processes

Everything from the last chapter was mechanism: the ability to drop into ring 3
and come back. This chapter builds the *model* on top of it — the Unix process
lifecycle of fork, execve, wait4, and exit — and, just as importantly, makes a
crashing process die alone without taking the kernel with it. This is where the
system finally looks like an operating system from the outside: programs that
start other programs, run in isolation, and are cleaned up when they end.

## 11.1 Loading a program: validate totally, then load trustingly

A program is a statically linked ELF64 executable (`ET_EXEC`, `EM_X86_64`).
Loading it splits into two functions with a deliberate, sharp division of
responsibility — and this split is a masterclass in how to handle untrusted
structured input.

**`elf64_validate()`** (`kernel/lib/elf64.c`) is a *pure* function over the image
bytes — host-tested under ASan/UBSan with a well-formed executable plus one
targeted mutation per rejection path. Its contract is strong and worth quoting in
spirit: after it returns OK, **every subsequent arithmetic step the loader
performs is provably in-bounds and overflow-free.** It checks the identity fields
(magic, class, endianness, version, type, machine), the program-header table
bounds, and for every `PT_LOAD` segment: file ranges in bounds *computed
overflow-safely*, `filesz ≤ memsz`, the virtual range inside
`[PAGE_SIZE, USER_VA_LIMIT)` (so the null page is never mappable and nothing
lands in the kernel half), vaddr/offset page congruence, no two segments sharing
a page, **no writable-and-executable segment** (W^X enforced at load, not just
runtime), and an entry point that lands inside an executable segment's
file-backed bytes.

**`elf64_load()`** (`kernel/proc/elf_load.c`) then materializes a *validated*
image without re-checking anything, because validation already proved it safe:
fresh zeroed frames per page (so bss tails and padding are zero by construction),
file bytes copied in, and leaf page permissions derived per segment — `PF_X`
clears NX, `PF_W` sets writable, everything else read-only-NX.

The pattern here is the one to internalize for *any* parser of hostile input:
**separate total validation from action, and make the validation a pure function
you can exhaustively test.** The validator's job is to establish an invariant
("this structure is well-formed and all these arithmetic relationships hold"); the
loader's job is to act under that invariant without defensive re-checking cluttering
every line. The mutation tests — take a valid image, corrupt exactly one field,
assert the validator rejects it — are how you gain confidence the validator is
*total*, i.e. rejects everything it should. A validator that is merely "usually
right" is worse than none, because it lulls the code downstream into trusting it.

`user/user.ld` links user programs at `0x400000` with three page-aligned
`PT_LOAD` segments — text R+X, rodata R, data+bss RW — precisely so the very
first binary exercises the per-segment W^X policy end to end.

## 11.2 The process table is a pure state machine

The process table itself (`kernel/proc/proc_core.c`, host-tested) is — you can
predict it now — a *pure* state machine: up to 64 processes, monotonically
increasing pids from 1 (init), parent links, a `LIVE → ZOMBIE → free` lifecycle,
and orphan reparenting to init when a parent exits. It touches no hardware, no
address spaces, no threads. That is what lets it be tested exhaustively on the
host, including the fiddly cases: reaping in various orders, reparenting a whole
subtree, pid wraparound behavior.

The kernel wrapper (`kernel/proc/process.c`) adds what a pure table cannot hold:
the address space, the hosting kernel thread, the blocked waiter, the process
name. This is the core/driver split applied to the process abstraction — the
lifecycle *logic* is pure and proven; the *bindings* to real kernel resources
are the thin trusted layer.

**Every process is hosted by a kernel thread.** The thread builds nothing itself:
it receives a completed image (address space plus start frame), binds to the
address space via `sched_set_addrspace`, and enters ring 3 through
`user_frame_enter` (Chapter 10), never to return except through syscalls. This is
the reuse dividend from Chapter 9 — because a process's kernel side *is* just a
thread, processes get preemption, sleep/wake, and join for free from the
scheduler, and the hosting thread simply carries its process's pid. Building the
process model on the thread abstraction instead of parallel to it is why this
phase is small.

## 11.3 The four syscalls, and the discipline in each

**`fork`** (syscall 57) allocates a child pid, clones the parent's address space
eagerly (`paging_user_clone`: every lower-half 4 KiB mapping copied to a fresh
frame with W/US/NX preserved — copy-on-write is a documented later optimization),
copies the parent's saved syscall frame with `rax = 0`, and launches a hosting
thread that resumes the child at the copied frame. Here the Chapter 10 groundwork
pays off completely: because the syscall frame is the *entire* user context laid
out as a struct, cloning execution state is a `memcpy` and a single field write.
The child returns 0, the parent returns the child's pid — same frame, one field
different.

**`execve`** (syscall 59) is the sharpest example of *build-new-then-commit* — a
transaction discipline you should reach for whenever an operation can fail
partway. It first copies the path and both string vectors *out* of user memory
(bounded: 16 args, 128 bytes of strings, path ≤ 63 — untrusted input gets hard
limits), then **builds the complete new image before touching the old one**: new
address space, ELF segments loaded, stack constructed, argv/envp marshalled. Only
once everything has succeeded does it commit — swap the address space, reload
`CR3`, destroy the old space, and rewrite the saved syscall frame so `sysret`
lands at the new entry point. The consequence: any failure (`-ENOENT`,
`-ENOMEM`, `-E2BIG`) returns with the calling process **completely unharmed**,
still running its old image. A half-replaced process is not a state that can
exist. If you take one design pattern from this chapter, take this: when an
operation mutates precious state and can fail, construct the entire replacement
off to the side and swap it in with one commit, so failure is always a clean
no-op.

**`exit`** (syscall 60) marks the process a zombie holding its status, reparents
its children to init, wakes the parent if it is blocked in `wait4`, and ends the
hosting thread.

**`wait4`** (syscall 61) finds a matching child (exact pid, or `-1` for any). A
zombie is reaped — join the hosting thread, destroy the address space, free the
table slot, and deliver the Linux wait status (`(code & 0xff) << 8` for a normal
exit; the signal number for a fault kill — real `WIFEXITED`/`WIFSIGNALED`
encoding). Otherwise the parent publishes itself as the waiter and blocks. The
critical detail: **the check for an existing zombie and the decision to block
happen in one interrupts-off section**, so a child exiting concurrently on
another thread cannot slip through the gap between "I looked and saw no zombie"
and "I went to sleep" — the classic *lost wakeup*. This is the single most common
concurrency bug in wait/notify code, and the fix is always the same shape: the
check and the sleep must be atomic with respect to the notify. Chapter 9's
interrupts-off lock makes that atomicity a two-line bracket.

## 11.4 Dying alone: fault isolation as the payoff

Back in Chapter 4 the trap dispatcher was built to check *who was running* when a
fault hit. Now that pays off. A hardware exception raised in **ring 3** is never
the kernel's problem:

> The dispatcher logs one diagnostic line (exception, `rip`, error code, `cr2`
> for page faults) and **kills the offending process** with the Linux signal for
> that exception — `#PF`/`#GP` → `SIGSEGV`, `#UD` → `SIGILL`, `#DE`/`#MF`/`#XM`
> → `SIGFPE` — delivered to the parent through `wait4`. The kernel and every
> other process keep running.

Only machine-level events (NMI, double fault, machine check) and *any* fault
taken in kernel mode still panic — a kernel-mode fault is a kernel bug by
definition, and there is nothing safe to do but stop. And if *init itself* dies
by signal, the kernel panics, Unix-style, because the system has lost its root
process.

This is isolation demonstrated, not asserted. Init deliberately forks children
that misbehave — one writes to a kernel address (killed with `SIGSEGV`), one
executes an illegal instruction (killed with `SIGILL`) — and reaps both through
`wait4` while everything else keeps running. The boot log shows the two fault
diagnostics sandwiched between normal output, and the run continues to
`boot: complete`. A crash that the rest of the system shrugs off, on every boot,
in the automated test.

## 11.5 Init as the acceptance test

Init (`user/init.c` plus `user/crt0.asm`) is not just the first process; it is
**the acceptance test for the entire userspace stack**, and its exit status names
the first failed check — 0 means all twenty passed. In one program it verifies:
`write` returns the full length; `.bss` is zero-filled and `.data` is initialized
and writable (proving the loader's segment handling); `getpid`; the `-ENOSYS`,
`-EBADF`, `-EFAULT` error paths; all six argument registers surviving a syscall
(proving the entry stub's register discipline from Chapter 10); the full process
round trip — fork returns a fresh pid, the child execve's `/bin/hello` which
checks its own argv arrived intact and exits 42, `wait4` returns that pid with
status `42 << 8`, a second `wait4` returns `-ECHILD`, `execve` of an unknown path
returns `-ENOENT`; and fault isolation itself. After init is reaped, the kernel
asserts the process table is empty and the physical frame count exactly matches
the pre-launch value: **the whole fork/exec/wait cycle leaked nothing.**

Making your first userspace program a comprehensive self-check of the layer
beneath it is a wonderful bootstrapping move. Init has to exist anyway; giving it
the job of exercising every syscall, every error path, and every leak invariant
means the most fundamental layer of the system is re-verified on every single
boot, by the very component that depends on it most.

## 11.6 The transferable lessons

- **Separate total validation from action.** A pure, exhaustively-tested
  validator establishes an invariant; the loader acts under it without defensive
  re-checking. Prove the validator is *total* with one-mutation-per-path tests.
- **Build-new-then-commit for fallible mutations.** `execve` constructs the whole
  new process off to the side and swaps it in atomically, so any failure leaves
  the caller untouched. A half-done state never exists.
- **Make check-and-block atomic to kill lost wakeups.** `wait4` looks for a
  zombie and goes to sleep in one interrupts-off section, closing the gap a
  concurrent exit could slip through.
- **Build the process on the thread, not beside it.** Hosting each process in a
  kernel thread inherits preemption, sleep, and join for free.
- **Let your first program test the layer under it.** Init doubles as the
  userspace acceptance suite, re-run on every boot.

The system now runs isolated C programs that start, fork, exec, crash safely, and
are reaped — but they still come from a blob embedded in the kernel, because
there is no disk to load them from. The last two subsystem chapters give the
kernel real storage and a real filesystem.

<div style="page-break-after: always"></div>

# Chapter 12 — Storage: PCI, virtio, and the Block Layer

To load a program from a disk, the kernel first has to *find* the disk on the
bus, learn how to talk to it, and build a layer that turns "the device" into "an
array of blocks I can read and write." This chapter is the storage stack from the
bus scan up: PCI enumeration, a modern virtio-blk driver written against the
VIRTIO 1.2 specification, the split-virtqueue mechanism underneath it, and a
caching block layer. It is also the chapter where "implement the published spec,
completely" replaces "invent something that works," because the device on the
other end obeys a standard and you must too.

## 12.1 PCI: finding what is on the bus

Devices announce themselves through PCI configuration space, reachable via the
legacy `0xCF8`/`0xCFC` I/O port pair (configuration mechanism #1). `pci_init()`
(`kernel/drivers/pci.c`) walks bus/device/function tuples, reading each function's
vendor and device IDs and class code. A vendor ID of `0xFFFF` means "no function
here." The boot log shows the enumeration:

```
pci: 00:04.0 1af4:1042 class 01.00
```

Vendor `1af4` is Red Hat / virtio; device `1042` is a modern virtio-blk device;
class `01.00` is a mass-storage block device. That one line is the kernel
discovering its disk. The scan is deliberately *flat* — enumerate functions,
match the ones you have drivers for — rather than a full recursive
bridge-traversal, because in the target (QEMU) environment the devices sit
directly on bus 0. That is complete-or-absent again: the scan covers exactly the
topology the system runs on, and a real-hardware bridge walk is additive later
work rather than speculative complexity now.

## 12.2 virtio: implementing a spec, not inventing an interface

The disk is a **virtio** device — the industry-standard paravirtual device
interface, the same one real cloud VMs use. It is worth being clear-eyed about
why: virtio is not a QEMU shortcut, it is a *published specification* (VIRTIO
1.2), and driving it correctly means implementing that spec faithfully, the same
way the ELF loader implements the ELF spec and the bootloader implements the BIOS
and long-mode contracts. "From scratch" in this project has never meant "make
something up"; it means "no third-party code," while every *external* interface is
implemented against its authoritative document.

QEMU is launched with `disable-legacy=on`, so it exposes the pure VIRTIO 1.x
interface (device id `1af4:1042`) and the driver does not implement the pre-1.0
legacy layout *at all*. Supporting one clean interface completely beats
half-supporting two. The modern transport (`virtio_pci.c`) discovers the device's
register regions through **PCI capabilities** — a linked list in config space
that points at the common, notify, and device-specific configuration structures —
and drives the standard bring-up handshake: reset the device, set the ACKNOWLEDGE
and DRIVER status bits, negotiate feature flags, set up the queues, then set
DRIVER_OK. Each of those steps is mandated by the spec and done in order; skipping
or reordering them is undefined behavior on the device side.

## 12.3 The virtqueue: the shared-memory ring at the heart of virtio

All virtio data transfer flows through **virtqueues** — split rings in memory
shared between driver and device. A split virtqueue has three parts: a
**descriptor table** (each entry: a physical buffer address, a length, flags, and
a `next` index to chain descriptors), an **available ring** (where the driver
publishes descriptor-chain heads it wants processed), and a **used ring** (where
the device publishes the chains it has finished). A disk request is a chain of
descriptors — a header (sector, read/write), the data buffer, and a status byte —
whose head the driver puts on the available ring; the driver then notifies the
device, the device performs the I/O and posts the head on the used ring.

The ring index arithmetic — allocating and freeing descriptors, wrapping the
available and used ring indices, matching completions back to requests — is
exactly the kind of intricate, off-by-one-prone bookkeeping that Chapter 1's
pattern exists for. So it is factored into a **pure** `virtq_core.c`, host-tested
in `tests/host/test_virtq.c` against a simulated device that plays the other side
of the ring. The bookkeeping is proven on the host under sanitizers; only the
MMIO doorbell writes and the physical-address plumbing are trusted to the kernel
driver. A shared-memory protocol with a device is precisely where a subtle ring
bug corrupts I/O in ways that are agonizing to debug on real hardware — so it is
the last place you want to be debugging without a safety net, and the first place
the pure-core discipline earns its cost.

`virtio_blk.c` builds on that: it assembles the three-descriptor request (header,
data, status), submits it, and — in this v1 — **polls** for completion with a
bounded timeout rather than sleeping on an interrupt. Polling is the simpler,
completely-implemented choice for a single synchronous requester; interrupt-driven
completion and request concurrency are noted as later work, gated behind a real
VFS that would have multiple callers.

## 12.4 The block layer: from a device to an array of blocks

Above the driver sits the block layer (`kernel/block/block.c`), which presents the
abstraction the filesystem actually wants: a flat array of fixed-size **4 KiB
blocks** you can read and write by number, over whatever driver happens to be
registered. Two design points matter.

First, **the block size is 4 KiB even though the device's native sector is 512
bytes.** 4 KiB matches the page size and the filesystem's block size, so a block
is a page is a filesystem block — one number everywhere, no impedance mismatch
between layers. Choosing your internal granularity to align across subsystem
boundaries eliminates a whole class of conversion bugs.

Second, there is a **write-through LRU cache** in front of the device (the boot
log: "cache 256 KiB"). Reads check the cache first; writes update the cache *and*
go straight to the device (write-through, so a crash never loses an acknowledged
write — there is no dirty data sitting only in RAM). LRU eviction keeps the
working set hot. Write-through rather than write-back is a deliberate durability-
over-throughput choice appropriate to a filesystem that is itself designed around
crash consistency (next chapter) — the cache must never become a place where
committed data can be lost.

A current simplification, honestly scoped: the block layer assumes a **single
caller at a time** until the VFS adds a sleeping lock. The comment says so at the
point it matters. When the VFS arrives with concurrent filesystem operations, a
proper lock replaces the assumption — the same "documented shortcut at the
load-bearing site" pattern as the scheduler's interrupts-off lock.

## 12.5 Proving the disk works before trusting it with a filesystem

`block_selftest()` runs on every boot and does a **write / readback / restore**
round trip against the *real* device: save a block's contents, write a known
pattern, read it back and verify, then restore the original bytes. The boot log:

```
block: selftest passed (write/readback/restore)
```

This is the storage stack's version of the recurring standard — do not assume the
device works, make it demonstrate a correct round trip on every boot, and do it
non-destructively so the test itself is safe to run against a live disk. Before
the filesystem stakes anything on the block layer, the block layer has proven,
this boot, on this hardware, that a byte written comes back.

## 12.6 The transferable lessons

- **Implement the spec, completely, for the one interface you target.** Modern
  virtio only, `disable-legacy=on`; the ELF/BIOS/VIRTIO contracts are all
  *implemented against their documents*, never approximated. "From scratch"
  means no borrowed code, not invented protocols.
- **The intricate ring bookkeeping is pure — host-test it.** A shared-memory
  device protocol is the worst place to debug an off-by-one on real hardware, so
  `virtq_core.c` is proven under sanitizers against a simulated peer.
- **Align granularity across layers.** 4 KiB block = page = filesystem block
  removes conversion bugs at every boundary.
- **Choose the cache policy that matches the durability contract.** Write-through
  keeps the cache from ever being a place committed data can be lost — the right
  call under a crash-consistent filesystem.
- **Make the device prove a round trip every boot.** Non-destructive
  write/readback/restore, asserted, before anything depends on it.

The kernel can now read and write blocks reliably. What it does not yet have is
*structure* — files, directories, names. Turning a flat array of blocks into a
filesystem, and doing it with the crash-consistency and integrity guarantees of a
modern design, is the final subsystem chapter.

<div style="page-break-after: always"></div>

# Chapter 13 — graphfs: A Filesystem From First Principles

The final subsystem is the one you are, at the time of writing, building: the
native filesystem. It is deliberately *not* a clone of ext2. It is a
copy-on-write, self-checksumming **property-graph** filesystem, and this chapter
is as much about *how to design an on-disk format* as it is about this particular
one. Designing a durable, corruption-resistant on-disk layout is one of the
deepest skills in systems programming, because the format is a contract with your
own future self across power failures and years of code changes — and unlike an
API, you cannot refactor it without a migration. The authoritative format spec is
Appendix K; this chapter is the reasoning behind it.

## 13.1 The design decision: a graph, not a tree

Most filesystems model a tree of directories containing files. graphfs models a
**property graph**: everything on disk is either a **node** (content plus
metadata) or a **typed, named edge** between nodes. The POSIX namespace is then
just *one edge type* layered on the graph — a "directory" is a node whose outgoing
`NAME` edges are its entries, and path resolution walks `NAME` edges by name. Two
other edge types, `TAG` and `REF`, are valid on disk today but ignored by path
resolution, reserved for a future AI-native semantic layer (provenance, semantic
links) with **no on-disk format change required**.

This is a strategic design bet identical in spirit to adopting the Linux syscall
ABI: build the substrate now so the future feature is a policy addition, not a
format migration. The most expensive thing to change in a storage system is the
format, so the format is where you pay for foresight. Even if the semantic layer
never ships, the cost was one more field in an edge record; if it does, an entire
class of "we can't do that without reformatting every disk" is avoided.

## 13.2 Copy-on-write: consistency without a journal

The core durability decision is that graphfs is **copy-on-write and never
overwrites live data**, in the ZFS/APFS mould. A change writes *fresh* blocks and
is made visible by a single atomic superblock write. The consequence is exact and
powerful:

> A power loss either lands **before** that superblock write — in which case the
> change never happened — or **after** it — in which case the change is whole.
> There is no journal, and no repair-on-boot fsck, because the on-disk image is
> *always* structurally consistent.

Compare the two classical approaches to crash consistency. A journaling
filesystem writes intended changes to a log first, then applies them, and replays
the log after a crash — correct, but every metadata change is written twice and
recovery is a process. Copy-on-write instead makes the *commit itself* atomic:
because nothing live is ever mutated in place, an interrupted operation leaves the
old version completely intact and simply invisible-in-progress. The design turns
crash consistency from an active recovery procedure into a *structural property*.
That is almost always the better kind of guarantee — one that holds by
construction rather than one you have to execute correctly under duress.

Making "a single superblock write commits everything" true requires that
everything the new state depends on is already durably on disk *before* the
superblock write, and that the superblock write is atomic. graphfs gets the
atomicity from **two superblock slots** selected by `generation & 1`: a commit
always writes the *inactive* slot at `generation + 1`, so the currently-live slot
is never touched, and mount simply takes the valid slot with the highest
generation. The allocation bitmap is likewise **double-buffered** by generation
parity. A commit never writes over anything the live filesystem is currently
relying on — which is exactly what makes an interrupted commit a no-op.

## 13.3 Self-validating checksums: detecting silent corruption

The second modern property is that graphfs is **self-checksumming**, and the
*placement* of the checksums is the clever part:

> Every metadata block is covered by a crc32c stored in the **pointer that
> reaches it**, not in the block itself. The superblock, having no parent,
> checksums itself.

Think about why "checksum in the pointer, not the block" is right. If a block
stored its own checksum, a write that was *misdirected* by the hardware — landing
the right bytes at the wrong address, a real failure mode — would carry a
perfectly valid self-checksum and be silently accepted. By putting each block's
checksum in its *parent* (a `struct gfs_bp = { phys, crc }`), the filesystem forms
a **self-validating tree**: to trust a block you must have arrived at it through a
parent that both named its location and attested its contents. Corruption — bit
rot, a misdirected write, a torn write — is caught on *read*, and served data is
data whose entire path from the superblock down was verified. This is the same
principle ZFS made famous, and it is a strictly stronger integrity guarantee than
in-block checksums.

The honest scope: v1 checksums *all metadata*; data-block checksums are a
documented later extension (exactly as btrfs shipped incrementally). The boundary
is stated, not blurred.

## 13.4 The layout, and what the constants encode

The on-disk geometry (full table in Appendix K, offsets in
`kernel/include/graphfs_core.h`) is: two superblock slots at LBA 0 and 1, then two
allocation-bitmap copies, then a copy-on-write region that begins with a node-map
block and the root directory's node-table block and grows on demand. 4 KiB blocks
throughout — page-sized, block-layer-block-sized, as Chapter 12 argued.

A few structural choices reveal how a format encodes its policies in constants:

- **Nodes are 256-byte records, 16 per block.** The node map is one 4 KiB block of
  16-byte checksummed pointers → at most 256 node-table blocks → **4096 nodes**
  (`GFS_MAX_NODES`). The maximum is not arbitrary; it is what the single-block node
  map can address. When a limit is a clean consequence of a structural choice, it
  is easy to reason about and easy to lift deliberately later (a multi-block node
  map).
- **Files are stored as extents** — runs of contiguous blocks — with **8 inline
  extents per node** and no extent tree in v1. So a file is at most 8 fragments
  (`GFS_EFRAG` past that), each up to 2³²−1 blocks. The cap is on *fragmentation*,
  not size, and a copy-on-write-friendly allocator keeps freshly written files to
  a single extent. This is a deliberate simplicity/capability trade with an
  explicit failure mode, not a lurking truncation bug.
- **Node types and edge types are small integers** (`NAMESPACE`/`DATA`;
  `NAME`/`TAG`/`REF`), and **all integers are little-endian**, stated once and
  obeyed everywhere. An on-disk format lives or dies by this kind of ruthless
  consistency, because every field is read by code that must agree byte-for-byte
  with the code that wrote it — possibly years apart.

## 13.5 Policy that is not format: single-parent namespaces

graphfs draws a careful line between what the *format* permits and what the
current *policy* allows — and understanding that line is a lesson in itself. The
v1 rule: a `NAMESPACE` node has exactly one incoming `NAME` edge (a single
parent), so `..` is a single stored field and directory cycles are *impossible*;
`DATA` nodes may have any number of incoming `NAME` edges (hard links). A second
`NAME` edge onto a namespace is rejected with `GFS_EMANYPARENTS`.

The crucial framing: this is a **link-time policy, not a format limitation.** The
on-disk structures could represent a multi-parent namespace graph perfectly well;
the *code* chooses to forbid it in v1 because single-parent directories make `..`
trivial and cycles unrepresentable, which eliminates a whole category of
path-resolution and garbage-collection hazards. Multi-parent namespaces are a
documented future extension that needs no format change. Distinguishing "the disk
can't represent this" from "the code currently chooses not to allow this" is
something juniors routinely conflate, and the distinction is exactly what tells
you whether a future feature is a policy tweak or a migration.

## 13.6 One format, three programs: the pattern at full strength

graphfs is the culmination of the pure-core discipline the whole book has built
toward. `kernel/fs/graphfs_core.c` is pure C over an abstract block-device
callback (`struct gfs_ops`), with **no kernel dependencies and no dynamic
allocation** — block scratch lives inside `struct gfs`, and the writable allocator
and fsck take caller-supplied buffers. That single implementation compiles into
*three* programs:

1. The **kernel**, which mounts the filesystem over virtio-blk.
2. `tools/graphfs_mkfs`, which creates an image and installs files at `/bin` — the
   build uses it to lay real `/bin/init` and `/bin/hello` onto `fs.img` instead of
   the embedded blob from Chapter 11.
3. `tools/graphfs_fsck`, which verifies an image; `make check-fsck` runs it on the
   freshly built filesystem, and the same check will run after boot as a
   crash-consistency gate.

Plus the host unit tests in `tests/host/test_graphfs.c`, under ASan/UBSan. The
enormous payoff: **the format has exactly one implementation.** mkfs cannot
disagree with the kernel about the layout, because they *are* the same code. fsck
cannot check a different format than the kernel writes. The host tests exercise
the identical bytes the kernel will mount. Every filesystem project's nightmare is
three subtly-diverging implementations of one format (the classic ext2 mkfs/fsck/
kernel drift); the pure-core discipline makes that divergence structurally
impossible. And because a copy-on-write, checksummed image is *always* consistent,
a healthy image *always* passes fsck — so an fsck failure means precisely a core
bug or media corruption, a sharp and useful signal rather than routine cleanup.

## 13.7 The transferable lessons

- **The format is where you pay for foresight.** You can refactor an API; you
  migrate a format. Building the graph substrate and the extra edge types now
  turns a future semantic layer into a policy addition, not a reformat.
- **Prefer guarantees that hold by construction.** Copy-on-write makes crash
  consistency a structural property — an interrupted commit is a no-op — instead
  of a recovery procedure you must execute correctly under duress.
- **Put integrity checksums in the pointer, not the block.** A self-validating
  tree catches misdirected and torn writes that in-block checksums accept.
- **Separate format capability from current policy.** Single-parent namespaces
  are a code choice over a format that could do more; know which of your limits
  are structural and which are policy.
- **One format, one implementation.** A pure core shared by kernel, mkfs, fsck,
  and tests makes the multi-implementation drift that plagues filesystems
  impossible.

That completes the system as it stands: from the firmware's first instruction to
a checksummed, crash-consistent filesystem holding the very programs the kernel
boots. The last two chapters step back — one to the testing philosophy that made
all of it trustworthy, and one to where the road goes from here.

<div style="page-break-after: always"></div>

# Chapter 14 — The VFS: One Namespace Over Many Filesystems

The previous two chapters built a disk and a filesystem format. What they did not
build is the thing a process actually uses: `open("/bin/hello", O_RDONLY)`. Between
"graphfs can resolve a path" and "a ring 3 program reads a file through a small
integer" sits the **virtual filesystem** — the layer that owns the namespace, the
open-file abstraction, and the file descriptor table, and that lets `/dev/console`
(a device) and `/bin/init` (a graphfs node) answer the same syscalls without the
caller knowing the difference. This chapter is that layer, and it is also the
chapter where the kernel grows its first *sleeping* lock and loads init from disk,
retiring the last scaffold from Phase 4.

## 14.1 Three objects, one discipline

The VFS (`kernel/fs/vfs.c`) is built from three small objects, each with one job:

- **`struct file_ops`** — a vtable of five operations (`read`, `write`, `lseek`,
  `fstat`, `getdents`). This is polymorphism in plain C: a graphfs regular file, a
  graphfs directory, the console device, and the `/dev` directory are four ops
  tables, and everything above them dispatches through the pointer without a
  single `if (is_device)` anywhere in the syscall layer. A `NULL` slot means "this
  operation does not apply here," and the syscall layer maps it to the
  conventional errno — `write` on a read-only file is `-EBADF`, `lseek` on the
  console is `-ESPIPE`, `getdents64` on a regular file is `-ENOTDIR`. The error
  vocabulary is part of the interface, decided once, at the boundary.
- **`struct file`** — an *open file description*, the POSIX term worth being
  precise about: it holds the ops pointer, the object identity, the **offset**,
  and a reference count. It is not the fd. When a process forks, parent and child
  fds point at the *same* description — read in one and the other's offset moves.
  That is not an accident of implementation; it is the POSIX contract that makes
  `fork` + shared logs work, and getting it right costs exactly one refcount.
- **The mount table** — compile-time in v1: graphfs on the root block device is
  `/`, and devfs covers `/dev`. Resolution is a longest-prefix match on the
  canonical path; whatever no mount claims belongs to the root filesystem. The
  `/dev` directory also exists *on disk* as an empty graphfs namespace
  (`graphfs_mkfs --dir /dev`), the way a mount point does on any Unix — the mount
  covers it, but `ls /` still lists it, because the namespace under a mount point
  is the on-disk one.

The fd table itself lives with the process (`struct process`, Chapter 11): a
small array of `struct file *`. `fork` duplicates the pointers and bumps
refcounts; `execve` deliberately leaves the table alone (no close-on-exec flags
yet — documented); exit closes everything. Init starts life with fds 0/1/2 all
referencing one open of `/dev/console` — one description, three references,
exactly what a login shell would inherit.

## 14.2 Path walking: the one pure piece, and why lexical ".." is honest here

Every path entering the VFS is first **normalized lexically** by
`vfs_path_norm()` (`kernel/fs/vfs_path.c`): duplicate slashes collapse, `.`
disappears, `..` pops a component, popping above the root stays at the root, and
the output is always a canonical absolute path. The function is pure C over two
buffers — no kernel dependencies — so it is host-tested under ASan/UBSan
(`tests/host/test_vfs_path.c`) with a table of canonical forms and every
rejection path, like every other core in the tree. Filesystems never see a `.` or
`..`: `gfs_resolve()` stays the simple plain-component walk Chapter 13 built.

Be honest about *when* this is correct. Lexical `..` handling is exactly right
here **because the graphfs v1 namespace is a strict tree with no symlinks**:
every directory has one name, so dropping the last component of a canonical path
*is* its parent. The day symlinks arrive, `/a/b/..` may no longer be `/a`, and
normalization has to move into the resolution walk. The comment at the top of
`vfs_path.c` says precisely this — a design decision pinned to the invariant that
justifies it, so the person who breaks the invariant finds the consequence
written where they will trip over it.

One more scoping decision of the same kind: there is no `chdir` yet, so every
process's working directory is defined to be `/` and relative paths resolve from
there. Not "relative paths are rejected," not "undefined" — defined, documented,
and replaced when cwd machinery arrives.

## 14.3 The kernel's first sleeping lock

Until now the kernel's only mutual exclusion was interrupts-off sections —
correct on one CPU, and fine for microsecond-scale work (Chapter 9). Disk I/O
breaks that model: a block read takes *milliseconds* and must keep interrupts on
(the driver polls a timer). Meanwhile the `struct gfs` handle is single-caller by
contract — its block scratch buffers cannot host two walks at once — and after
this chapter, *every process in the system* can reach the disk through a syscall.
Two forked children execing simultaneously would interleave core calls and
corrupt the walk state.

The answer is the kernel's first **sleeping mutex** (`kernel/sched/mutex.c`),
~70 lines over the primitives Chapter 9 already proved: contenders queue FIFO and
`sched_block()`; `mutex_unlock()` *hands the lock to the oldest waiter* before
waking it. The handoff detail is worth noticing — the lock is never observably
free while anyone queues, so a fresh arrival cannot barge past a sleeper, which
is FIFO fairness and starvation-freedom in two lines. The waiter queue reuses
`thread->next`, which a blocked thread is provably not using — the same
"a thread is on at most one list" invariant from Chapter 9, cashed in.

One global `fs_lock` inside `vfs.c` serializes every graphfs core call and every
file-offset update. That single lock also **discharges the block layer's
documented debt**: Chapter 12 left `block.c` asserting "one caller at a time"
with a note that the VFS would bring the real lock, and now it has — the comment
in `block.h` records the discharge. A promissory note in a comment is only
honest if someone actually pays it; slice 5c was where this one came due.

## 14.4 devfs and the blocking read: the lost-wakeup pattern, third appearance

devfs (`kernel/fs/devfs.c`) is deliberately tiny: `/dev/console` and the `/dev`
directory itself, each an ops table and a synthetic inode number. Console writes
go to the kernel console. Console *reads* are the interesting part: they must
**block** until the keyboard produces input — the first time a syscall sleeps on
a device.

The race to beat is the same lost wakeup that `wait4` fought in Chapter 11: if
the reader checks the buffer (empty), and the keyboard interrupt fires *before*
the reader sleeps, the wakeup is lost and the reader sleeps forever. The cure is
also the same, because it is *the* cure: publish the waiter and block inside one
interrupts-off section. The keyboard IRQ handler gets a one-line notify hook
(`keyboard_set_notify`) that wakes the published reader. By the third appearance
— join, wait4, now console read — the pattern should feel like an instinct:
**check-and-sleep must be atomic against the waker.**

Read semantics are terminal-style: block until at least one character exists,
then return what the buffer holds (a short read), because an interactive caller
asking for 100 bytes wants the 3 you have now, not a wait for 97 more. A second
lock (`read_lock`) serializes concurrent readers so the single waiter slot
suffices — a scoped simplification that holds until someone actually wants
competing console readers.

## 14.5 exec from disk: the scaffold comes down on schedule

Chapter 2 called the ELF images embedded in kernel `.rodata` "an honest scaffold
with a clear expiry date." This is the expiry date. `process_execve()` and
`process_run_init()` now call `vfs_read_file()` — resolve, size, read the whole
image into a kernel buffer, load, free — and the built-in program table,
`kernel/user_blob.asm`, and the `incbin` build rule are *deleted*. The boot
marker changes from `launching init (embedded ELF)` to:

```
user: launching init (/bin/init from disk, 13448 bytes)
```

and the integration test asserts the new text — the boot now *proves* init came
off the graphfs image, because there is no other place it could come from.

The syscall surface grows to match, and every new syscall keeps the Linux x86_64
ABI bit-for-bit: `read` 0, `open` 2, `close` 3, `fstat` 5, `lseek` 8,
`getdents64` 217, alongside the existing numbers. `struct stat` is the Linux
144-byte layout (`_Static_assert`ed), `getdents64` emits real `linux_dirent64`
records, and directories synthesize `.` and `..` entries the way readdir
consumers expect. None of this costs more than inventing a private layout would,
and it is exactly what makes the Phase 7 personality layer a numbering no-op
(Appendix H tabulates the whole surface).

## 14.6 Proving it: the acceptance suite grows 27 checks

Init's role as the boot acceptance test (Chapter 11) extends to the whole VFS
read side — checks 21 through 47 open a known file and verify its ELF magic,
prove `lseek END/SET` against `fstat`'s size, walk `/bin` with `getdents64` and
require *exactly* `.`, `..`, `init`, `hello` and nothing else, then probe every
error contract: `-ENOENT` for a missing name, `-ENOTDIR` for a path through a
file, `-EISDIR` for reading a directory, `-EROFS` for opening anything on disk
for write (the mount is read-only until 5d), `-EBADF` after close, `-ESPIPE`
for seeking the console. A path like `/dev/../bin/./hello` must resolve — the
normalizer's host tests re-proven end-to-end from ring 3. The exit status names
the first failure; the boot test asserts 27 serial markers and `status 0`.

The read side of a filesystem is the half you can prove without being able to
mutate anything; the write path (`write`, `mkdir`, `link`, `unlink`, `rename`,
`fsync`) is slice 5d, where the fsck-after-boot gate turns every future boot
test into a crash-consistency test.

## 14.7 The transferable lessons

- **An ops vtable plus a refcounted description is the whole VFS trick.** Four
  ops tables, one dispatch site, POSIX offset-sharing from one refcount — no
  type switches anywhere above the filesystems.
- **Pin a lexical shortcut to the invariant that makes it sound.** Lexical `..`
  is *correct* under a strict tree with no symlinks; the comment names the
  invariant so its removal takes the shortcut with it.
- **When critical sections learn to sleep, interrupts-off stops being a lock.**
  Millisecond I/O plus multiple callers forced the first real mutex — and
  unlock-by-handoff bought fairness for free.
- **Pay your documented debts, and record the payment.** The block layer's
  "single caller until the VFS" note was a promissory note; the discharge is
  written where the promise was.
- **The same race deserves the same cure, every time.** Publish-then-block
  interrupts-off killed the lost wakeup in join, wait4, and now console read.
  Collect patterns, not incidents.
- **Let the acceptance suite grow with the surface.** Every new syscall landed
  with checks a boot cannot pass without executing them.

The kernel now walks one namespace from `/` to devices and disk files, and
nothing it runs is embedded in it. What remains of Phase 5 is teaching the
namespace to change — the write path — with the crash-consistency gate that
graphfs's copy-on-write design was built to pass.

<div style="page-break-after: always"></div>

# Chapter 15 — Testing and Professional Discipline

Every subsystem chapter ended with the machine proving something on every boot.
This chapter steps back and treats that as the subject in its own right, because
the testing strategy is not a supporting character in this project — it is the
main reason the project can exist at all. An OS is too large to hold in your head
and too unforgiving to debug by running it once. The thing that lets you build it
anyway is a test architecture that makes correctness *cumulative*: every property
you establish stays established forever. If you internalize one chapter's *method*
rather than its facts, make it this one.

## 15.1 Three levels, three kinds of truth

`make check` is the gate for every commit, and it runs three levels of testing
that catch three genuinely different classes of bug (Appendix L).

**Level 1 — host unit tests, under sanitizers.** The arch-neutral *pure cores*
(`string.c`, `fmt.c`, `pmm_core.c`, `heap_core.c`, `sched_core.c`, `elf64.c`,
`proc_core.c`, `virtq_core.c`, `crc32c.c`, `graphfs_core.c`) are compiled *for
macOS* and run under `-fsanitize=address,undefined
-fno-sanitize-recover=all`. This is the payoff of the pure-core discipline stated
as a testing fact: AddressSanitizer and UBSan **cannot run in a freestanding
kernel build** — they need a host runtime — so the only way to get their scrutiny
on your kernel logic is to make that logic host-compilable. Every off-by-one,
every signed overflow, every use-after-free in the core logic is caught here, on
your laptop, with a stack trace, before it ever reaches the metal.

The mechanics are worth knowing because they are reusable. The kernel sources are
compiled with their public symbols **renamed** (`-Dmemcpy=hl_memcpy`, ...) so they
cannot collide with the host's libc or the sanitizer runtime; the test sources get
the same renames, so a plain `memcpy(...)` in a test resolves to the kernel's
implementation under test. `-fno-builtin -D_FORTIFY_SOURCE=0` stops the host
compiler from replacing or wrapping the very calls being tested. The framework
(`tests/host/test.h`) registers tests via constructor attributes and reports every
failure with file and line *without aborting*, so one run surfaces all failures,
not just the first. This is how you run kernel code in a hosted sanitizer without
the two environments contaminating each other.

**Level 2 — in-kernel self-tests.** `selftest.c` runs a boot-time assertion suite
compiled by the *real* kernel toolchain (`--target=x86_64-elf`, `-mcmodel=kernel`,
no SSE). Its job is different from level 1: it catches **codegen- and
environment-specific** breakage the host build cannot — a bug that only appears
under the kernel's code model, its calling convention, its lack of SSE, its actual
page tables. It exercises the library, traps (`int3`), the PMM, the VMM
protections (provoking a fault against a read-only page and confirming it faults),
the heap, and the scheduler's interleaving. Self-tests are cheap by design because
they run on every boot; heavy stress goes at level 1 or behind a dedicated
scenario.

**Level 3 — the QEMU integration test.** `tests/run_qemu.py` boots the *actual
disk image* headless and asserts that a specific list of markers appears on the
serial console **in order** — one marker per proven subsystem, from the banner
through memory, scheduling, the self-tests, the ring-3 round trip (`hello from
ring 3` is printed *by user code*), to `boot: complete`. It fails immediately if
`PANIC` (kernel) or `ERR:` (bootloader) ever appears, or on a timeout. This is the
only level that tests the *whole system integrated on real-ish hardware* — the
bootloader, the long-mode transition, the drivers talking to emulated devices, the
end-to-end boot.

Three levels, three truths: **logic** (level 1, sanitized), **codegen/environment**
(level 2, real toolchain), **integration** (level 3, real boot). A bug lives in
exactly one of those categories, and the architecture has a net under each.

## 15.2 Markers as an append-only ledger of proven behavior

The integration test keys on serial-console markers, and the governing rule is:
**existing markers are never removed, only added to.** Each marker is a permanent
assertion that one subsystem still works. `sched: online`, `selftest: passed`,
`block: selftest passed`, `hello from ring 3`, `boot: complete` — every one is a
property proven at some phase and re-proven on every boot forever after.

The effect is a **ratchet**. A regression in Phase 2's memory protection cannot
ship in Phase 5, because Phase 5 still boots through the Phase 2 marker that
asserts it. This is what makes correctness cumulative rather than a game of
whack-a-mole where fixing one thing breaks another silently. The design principle
generalizes far beyond kernels: make your regression suite an **append-only ledger
of things that once worked**, and never delete an assertion because the feature it
covers is "old and stable" — old and stable is exactly what silently breaks.

## 15.3 Design your failures to be loud and machine-detectable

A test harness can only catch failures it can *see*. This project's bootloader and
kernel are written so that **every fatal path emits a detectable pattern** — `ERR:`
from the bootloader, `PANIC` from the kernel — and the harness treats either as
immediate failure. The consequence, stated in the testing doc: "a hang without
output is the only failure mode the harness can attribute solely to a timeout."

That sentence is a design *goal* working backward. Because every fatal path is
loud, a silent hang is *diagnostic* — it means something wedged without reaching a
known failure point, which is itself information. You get that property only by
being disciplined at every error site: never fail silently, never return a
plausible wrong value, always emit the pattern the harness knows. This connects
straight back to Chapter 1's complete-or-absent — an explicit `-ENOSYS`, a
`GFS_EFRAG`, a `panic()` with file and line are all the same instinct: **make the
failure impossible to miss.** Code that fails loudly is code you can build a robot
around; code that fails silently is code you have to babysit forever.

## 15.4 The policy that ties it together

Three rules govern the whole project (Appendix L), and they are worth
adopting verbatim:

1. **`make check` must pass before every commit.** The gate is not advisory. Green
   or it does not land.
2. **A bug fix lands with a test that failed before the fix.** This is the single
   highest-leverage habit in all of software. It proves the fix works *and*
   permanently prevents the bug's return, converting every bug you fix into a
   brick in the wall. A fix without a test is a fix you will make again.
3. **A feature is not done until it is covered at the appropriate level(s):** pure
   logic at level 1, boot-visible behavior at level 3, invariants at level 2. This
   is what "done" *means* here — not "it works when I run it," but "the machine
   demonstrates it works, and will keep demonstrating it."

Underneath sit the static gates: `-Wall -Wextra -Werror` on every compile (the
compiler's static analysis is a safety net you do not get to decline),
`make format-check` for mechanical consistency, and `make tidy` (clang-tidy with
the real kernel flags). These are cheap, automatic, and non-negotiable — the point
of a mechanical gate is that it removes an entire category of judgment call and
bikeshedding from every code review.

## 15.5 The design doc: deciding on paper, where changing your mind is free

There is a second discipline running alongside the tests, easy to miss
because its artifacts look like documentation: **every subsystem was
designed in writing before or with its code, and the writing is maintained
as part of the code.** Nine documents live in `docs/` — the boot protocol,
the memory map, scheduling, userspace, storage, graphfs, the test strategy
itself — and the commit ledger shows them landing *with* their subsystems,
then updated in the same commit as every change ("Docs updated ... Version
0.4.1"). This is a skill no OS book teaches, so learn it from the artifacts:
open Appendix G and read its section list as a template —

1. **Layering** — what sits above and below, and which direction calls flow.
2. **The model** — the states and structures, in plain declarative sentences.
3. **The mechanism** — context switch, preemption: how, and crucially *when*.
4. **Invariants** — the one-sentence claims of Chapter 1 §1.4, numbered.
5. **The locking story** — what protects shared state, and the honest note
   that these sections become real spinlocks when SMP arrives.
6. **The proof** — how the machine demonstrates the design works. The test
   plan is part of the design, not an afterthought.

That outline is the checklist of questions a subsystem design must answer
*before* implementation: interface and ownership, invariants, atomicity,
documented limits, and proof. Answering them on paper costs an hour and
lets you discover the ordering hazard or the missing invariant while it is
still a sentence you can rewrite — instead of a use-after-free you have to
hunt (the entire zombie-stack discipline of Chapter 9 §9.4 is one design-doc
sentence enforcing itself). Notice also what these docs are *not*: they do
not restate the code, and they are not stale visions of what the code was
once meant to be. A design doc that can drift from its code is worse than
none, which is exactly why "docs updated" appears in the ledger next to
every feature — same commit, same review, same gate. And when a document
describes an interface two programs must agree on, it gets a version:
Appendix E is "boot protocol v1," a numbered contract between
loader and kernel (Chapter 3 §3.4). Comments hold the local, load-bearing
constraint at the line that needs it; design docs hold the shape of the
subsystem; commit messages hold the change and its proof. Three channels,
three jobs — a codebase that uses all three deliberately is one you can
join, or return to after a year, without an oral tradition.

## 15.6 Why this is the chapter that matters most

Here is the honest truth about building something this large and unforgiving: you
will not write it correctly the first time. Nobody does. The bootloader's A20
handling, the scheduler's zombie-reaping ordering, the syscall entry's register
discipline, the filesystem's commit atomicity — every one of these has a dozen
ways to be subtly wrong, and many of those ways work fine until the exact
condition that breaks them.

What separates a great systems programmer from a merely competent one is *not*
getting it right the first time. It is building the machinery that (a) catches the
mistake close to where you made it, (b) tells you exactly what broke, and (c)
ensures that once fixed, it is fixed forever. The pure cores exist so sanitizers
can find the logic bugs. The self-tests exist so the real toolchain's quirks
surface at boot. The integration markers exist so no old guarantee silently rots.
The loud-failure discipline exists so the robot can tell success from failure. The
commit gate exists so none of it can be skipped under deadline pressure.

Master this and everything else in the book becomes *achievable*, because you no
longer need to be perfect — you need to be rigorous, and rigor is a system you
build, not a talent you are born with.

## 15.7 The transferable lessons

- **Test at three levels for three kinds of bug:** sanitized host tests for
  logic, in-kernel self-tests for codegen/environment, integration boot for the
  whole system. Know which net catches which fall.
- **Make regression assertions append-only.** Never delete a marker; old-and-
  stable is what silently breaks. Correctness must be cumulative.
- **Engineer every failure to be loud and machine-detectable**, so that silence
  itself becomes diagnostic and the whole thing can be automated.
- **Every bug fix ships with a test that failed before it.** This one habit,
  more than any other, is what compounds into a codebase you can trust.
- **"Done" means the machine demonstrates it and keeps demonstrating it** — not
  that it worked once when you tried it.
- **Design on paper first — interface, invariants, locking, limits, proof —
  and keep the paper in the same commits as the code.** A decision is
  cheapest to change while it is still a sentence.

<div style="page-break-after: always"></div>

# Chapter 16 — How an OS Actually Gets Built: Slices, Gates, and the Definition of Done

Every OS book — this one included, until now — presents subsystems in their
finished form, ordered by dependency: memory before scheduling, scheduling
before processes. That ordering is true but it answers the wrong question.
The question you actually face on day one of your own kernel is not "how
does a scheduler work" — it is *what do I build first, how much of it, and
how do I know when to stop and move on?* Sequencing is the invisible half of
systems engineering, and almost nobody teaches it because almost no codebase
preserves the evidence.

This one does. The git history of this repository is a complete, unedited
ledger: nineteen code commits from empty directory to a filesystem, every
one of them ending with `make check` green, every message recording what
landed and what proved it. This chapter reads that ledger as what it is —
a decision journal — and extracts the sequencing discipline from it.

## 16.1 The ledger

Read down this table slowly; the order itself is the curriculum.

| # | Commit | What landed | What proved it |
|---|--------|-------------|----------------|
| 1 | `build:` | Makefile, `-Werror`, format/tidy gates, sanitizer test scaffold | the gates themselves |
| 2 | `kernel/lib:` | freestanding `string.c` + full C99 `vsnprintf` | 25 host tests, 170 assertions, ASan/UBSan |
| 3 | `boot:` | two-stage bootloader: A20, E820, unreal-mode load, long mode | every failure path prints `ERR:` |
| 4 | `kernel:` | higher-half entry, serial+VGA consoles, `kprintf`, `panic`, selftest | boot-time assertion suite |
| 5 | `tools+tests:` | disk-image assembler, headless QEMU harness | ordered serial markers, fail on `PANIC`/`ERR:` |
| 6 | `docs:` | README, boot protocol, architecture, memory map, test strategy | — (the contracts, in writing) |
| 7 | `arch:` | GDT/TSS (IST for `#DF`), 256-vector IDT, trap dispatch, PIC | `int3` round trip in selftest |
| 8 | `drivers:` | IRQ layer, 100 Hz PIT, PS/2 keyboard | timer marker; echo loop |
| 9 | `mm:` | physical frame allocator over E820 | 3,318 host assertions; alloc/write/free selftest |
| 10 | `mm:` | kernel page tables: HHDM, W^X image, NX | W^X verified by provoked faults at boot |
| 11 | `mm:` | slab heap | 20k-round randomized stress vs. shadow model |
| 12 | `sched:` | threads, context switch, preemption, sleep/join | `"abcabcabcabc"` interleave marker |
| 13 | `user:` | ring 3, `SYSCALL`/`SYSRET`, user address spaces | `hello from ring 3` — printed by a 92-byte flat binary |
| 14 | `exec:` | ELF64 validator + loader, C userspace with own toolchain rules | 7 new host suites; C init's exit status asserts the ABI |
| 15 | `syscall:` | full caller-saved register preservation (a found latent bug) | sentinel-register test; old stub demonstrably fails it |
| 16 | `proc:` | fork, execve, wait4, exit | fork/exec/wait round trip; frame-leak accounting to zero |
| 17 | `trap:` | ring-3 faults kill the process, not the kernel | two deliberately crashed children, reaped with correct signals |
| 18 | `storage:` | PCI scan, virtio-blk, cached block layer | write/readback/restore against the real device |
| 19 | `fs:` | graphfs on-disk format core, mkfs/fsck | 77 host tests; `make check-fsck` gates the built image |

Three phases of reading. First pass: notice it is the dependency order you
already know. Second pass: notice what is *interleaved* with the features —
gates first, library before bootloader, harness immediately after first
boot, docs before the second phase, a bug-fix slice in the middle of Phase
4\. Third pass: notice the right-hand column is never empty (except the
docs commit — whose artifact *is* the proof). That column is the subject of
§16.4.

## 16.2 The net went up before the wire

The single most instructive fact in the ledger is commit 2. Before there
was a bootloader — before this project could execute *anything* on the
machine it targets — it wrote `memcpy` and `vsnprintf` and put them under
AddressSanitizer on the host. The first code written for the freestanding
x86-64 kernel ran first on a Mac, under a sanitizer, called by a unit test.

That looks backwards until you weigh what each piece is *for*. The
bootloader is going to fail — real-mode assembly always does — and when it
fails, the only diagnostic channel will be whatever the kernel can print.
So the printing machinery must already be trustworthy, and the only place
to make it trustworthy before a kernel exists is the host. Commit 2 is the
pure-core pattern (Chapter 1 §1.2) applied to *sequencing*: build each
tool's safety net before the work that will need the tool.

The same logic explains commit 5. The instant there was one bootable image,
the next commit was not a feature — it was the harness that boots that
image headless and asserts its output. From that moment forward, *nothing
was ever demonstrated by hand again.* Every subsequent row in the ledger
inherits an automated definition of "still works." The cost of the harness
was one commit; the return is that eighteen commits of accumulated behavior
are re-proven on every `make check` forever (Chapter 15's ratchet, seen
from the other end: the ratchet had to be *installed early* to be worth
anything).

And commit 6 — pure documentation — lands before the first line of Phase 2.
The boot protocol was written down as a versioned contract at the moment
exactly two programs depended on it and both were fresh in mind. Contracts
are cheapest to write at the boundary where they were just negotiated.

## 16.3 Vertical slices, and the smallest observable win

Look at how Phase 4 — userspace — is cut. A lesser plan says "implement
processes": ELF loading, fork, exec, wait, fault handling, all of it, then
debug the pile. The ledger instead shows four slices, each independently
observable:

1. **Commit 13: a 92-byte flat binary.** Not ELF. Not C. A hand-assembled
   blob embedded in kernel `.rodata`, copied into a fresh address space,
   entered via `iretq`. Its entire job is to prove the *scariest single
   transition in the project* — ring 3 entry, `SYSCALL` return, per-thread
   kernel stacks, address-space isolation — with the fewest moving parts
   that can possibly print `hello from ring 3`. Every part of the ELF
   pipeline it omits is a part that *cannot be the bug* when the ring
   transition misbehaves.
2. **Commit 14: now ELF, now C.** With the ring transition proven and
   marker-pinned, the program format graduates: a real validator (pure,
   host-tested, one targeted mutation test per rejection path), a real
   loader, a real C runtime. If something breaks now, it is in the new
   layer — the old marker still passing says the transition underneath is
   intact. This is bisection *built into the schedule*.
3. **Commit 16: fork/exec/wait**, standing on both. The process table is a
   pure state machine (host-tested), and the syscall frame from slice 15
   turns fork's "clone the user context" into a struct copy.
4. **Commit 17: fault isolation** — the hardening slice that closes the
   phase's documented gap, converting "a buggy process panics the kernel"
   into one diagnostic line and a `SIGSEGV` delivered through `wait4`.

Each slice ends at a *marker* — an observable, machine-checkable behavior
that did not exist before it. That is what "vertical slice" means here: not
"a layer," but a path all the way through the system to something you can
see. The discipline generalizes to every phase in the ledger: the block
layer landed with a write/readback/restore round trip against a real
device *before* any filesystem existed to need it; graphfs landed as an
on-disk format with host tools and tests *before* the kernel could mount
it. Always cut so that the end of the slice is a demonstration.

When you plan your own next step, the question is not "what component comes
next" but **"what is the smallest thing I could build that the serial
console could prove?"** — and then: what is the least machinery that
demonstration needs? The 92-byte binary is the canonical answer. It is not
a toy; it is a scope decision of professional precision — small enough that
ring-transition bugs have nowhere to hide, real enough that the marker it
prints is asserted forever after.

## 16.4 The definition of done, in the authors' own hand

Read the closing lines of the ledger's commit messages, because they are a
definition of "done" stated nineteen times:

> "Host tests: 37/37, 3318 assertions."
> "Proven on every boot and asserted by the integration test."
> "Verified the test catches the regression: with the kill path disabled,
> the same boot dies in a kernel panic at the child's page fault."
> "After init is reaped the kernel asserts the process table is empty and
> the physical frame count matches the pre-launch value."
> "Docs updated ... Version 0.4.1. Phase 4 complete."

Four ingredients recur. **Proof at the right level** — pure logic cites host
assertions, boot-visible behavior cites markers (Chapter 15 §15.4's rule 3,
applied per commit, not per release). **Proof of the proof** — commit 17
did not just add a test; it broke the fix on purpose and recorded that the
test fails, because a test you have never seen fail proves nothing.
**Conservation checks** — "frame count matches pre-launch" is a *leak
ledger*: the whole fork/exec/wait cycle must return the machine to its
exact prior accounting. And **the paperwork** — docs and version roll in
the same commit, because a feature whose documentation lags is not done, it
is *undone in a way you have not noticed yet*.

Notice also what commit 15 is: not a feature. A latent bug (Appendix C
§C.1) was found mid-phase, and the response was a dedicated slice — fix,
test that fails without it, contract documented — inserted into the
sequence before fork was allowed to build on the flawed frame. Sequencing
is not just ordering the features; it is *granting hardening the same
slice-level dignity as features*, at the moment the debt is found, not "in
a cleanup pass later" that history shows never comes.

## 16.5 Changing your mind is a slice, too

Commit 18 opens with something you will almost never see preserved:
"The direction for this phase changed by decision: the native filesystem
will be graphfs ... not ext2. ext2 moves to Phase 7 as an optional import
path."

The original roadmap said ext2. Between phases, the plan changed — for
product reasons (the AI-native semantic layer of Phase 6 wants a graph
substrate, Chapter 13). What the ledger teaches is not *that* plans change
— everyone knows that — but the *mechanics* of changing one well:

- **The pivot happened at a slice boundary**, not mid-implementation. No
  half-built ext2 was thrown away, because Phase 5's first slice (PCI,
  virtio, block layer) was deliberately *filesystem-agnostic* — the layer
  boundary held, so the decision above it stayed cheap to revisit.
- **The displaced work was re-scoped, not deleted.** ext2 moved to Phase 7
  with a stated role. A plan is a priority queue, not a promise.
- **The decision was recorded where the code landed** — first line of the
  commit, plus a design doc (now Appendix I) written before the slice.
  Two years from now, "why graphfs and not ext2" has an answer in `git log`
  instead of in someone's departed memory. (Chapter 0's advice to read real
  kernels' changelogs is this same channel, consumed from the other side.)

## 16.6 Hooks, not half-features

Sequencing has a paradox: you must not build ahead (complete-or-absent,
Chapter 1 §1.3), yet the ledger is full of Phase-2 decisions that Phase-4
features "finally earn" — the GDT selector layout arranged for `SYSRET`'s
`+8/+16` arithmetic two phases early; the syscall ABI matched to Linux five
phases before the compatibility layer; `kstack_top` exported to a scheduler
three chapters away; `TAG`/`REF` edge types reserved in the graphfs format
for Phase 6.

The resolution is *where* the foresight lives. Every one of those is a
**hook**: a choice made at an interface — an ordering, a constant, a
reserved value, an exported symbol — that costs nothing now and cannot rot,
because it has no moving parts. None of them is a half-feature: there is no
dormant SMP path, no stubbed-out COW handler, no "TODO: ext2" directory.
The rule the ledger models: **spend foresight freely on interfaces, never
on implementations.** An interface shaped for tomorrow is a constraint you
carry; an implementation built for tomorrow is a liability you debug.
(Appendix A's fork entry shows the same rule from the other side: COW was
*not* built early, precisely because it is an implementation, not an
interface.)

## 16.7 Scoping your own next slice

Distilled from the ledger, the questions to answer — in writing, before the
first line of code — when you cut your next slice on your own kernel:

1. **What will the serial console say when this works?** If you cannot name
   the marker, the slice has no observable end and will sprawl.
2. **What is the least machinery that demonstration needs?** Everything
   else you are tempted to include is a place for bugs to hide behind other
   bugs. (The answer can be as small as 92 bytes.)
3. **Which part is pure?** That part gets a `_core.c`, host tests, and a
   sanitizer before it ever runs on metal.
4. **What is explicitly out?** Written down, with the failure mode for
   anything outside the boundary (`-ENOSYS`, a documented limit). "Out" is
   a deliverable, not an omission.
5. **What does the layer below have to promise?** If the promise is not
   already written down, writing it down is part of *this* slice — that is
   how the boot protocol (Appendix E) and the syscall ABI contract (Appendix H) came to exist.
6. **What would prove a regression?** The marker or test this slice adds is
   permanent (Chapter 15 §15.2); name what it will catch.

Answer those six and the slice plans itself. Most of what looks like
engineering judgment in the ledger is these questions, asked every time,
answered honestly.

## 16.8 The transferable lessons

- **Sequence so that every step's failure is debuggable with the tools of
  the previous step.** Library under sanitizers, then bootloader with
  `ERR:` prints, then a kernel that can `kprintf`, then a harness that
  reads it — each layer is the diagnostic channel for the next.
- **Automate the demonstration the moment there is one.** The harness
  commit, one slot after first boot, is what turned nineteen commits of
  behavior into a ratchet instead of a memory.
- **Cut slices vertically, ending at something observable.** "The smallest
  thing the serial console can prove" beats "the next component" as a unit
  of work, every time.
- **"Done" is a proof, a proof of the proof, a conservation check, and the
  paperwork** — per commit, in the commit.
- **Change plans at slice boundaries, in writing, where the code lives.**
  And keep the layer boundaries honest so that changing the plan stays
  cheap.
- **Foresight goes in interfaces; implementations stay in the present.**
  Hooks are free forever; half-features are debt from day one.

One more thing the ledger shows: it took nineteen commits to get from an
empty directory to a copy-on-write filesystem — not nineteen hundred. The
sequencing discipline is not overhead on the real work; it is why the real
work compounded instead of collapsing. Where the road goes from here — the
VFS, the AI service layer, Linux compatibility, real hardware — is the
final chapter's subject.

<div style="page-break-after: always"></div>

# Chapter 17 — Where to Go Next

You have followed the system from the firmware's first instruction to a
crash-consistent filesystem holding the programs the kernel boots. That is a
complete vertical slice of an operating system — bootloader, memory, scheduling,
userspace, storage, filesystem — and understanding it end to end puts you well
past where most people who "know OS concepts" actually are, because you have seen
how the concepts *connect* rather than as isolated exam answers. This final
chapter is about turning that understanding into mastery: what to build next, and
how to keep growing.

## 17.1 The road this project is on

The roadmap (README) runs to phase 10, and each remaining phase is a chance to
learn a major subsystem:

- **Finish Phase 5 — the VFS.** The filesystem core exists; the next slices add a
  virtual filesystem layer (a `struct file`, fd tables, `open`/`read`/`close`/
  `fstat`/`lseek`/`getdents64`), a device filesystem, and *exec-from-disk* — at
  which point the embedded program blob from Chapter 11 disappears and the kernel
  loads `/bin/init` off graphfs. Then the write path and a fsck-after-boot
  crash-consistency gate. Building a VFS teaches you the art of the *right
  abstraction*: one interface over graphfs, devfs, and eventually a network
  filesystem, without leaking any of their differences upward.
- **Phase 6 — AI as a system service.** A privileged userspace daemon bridged to
  a host-side helper over virtio-serial, exposed to every process via `/dev/ai`
  and dedicated syscalls. This is where the `TAG`/`REF` edges designed into
  graphfs in Chapter 13 stop being reserved and start carrying provenance and
  semantic links. The design constraint — AI is a userspace service, *never* in
  kernel space — is itself a lesson in keeping the deterministic core deterministic.
- **Phase 7 — Linux binary compatibility.** A Linux syscall personality layer so
  unmodified static musl binaries (busybox first) run natively. This is why the
  native ABI *is* the Linux ABI (Chapter 10): the groundwork was laid five phases
  early. You will learn how FreeBSD and managarm actually run Linux programs.
- **Phases 8–10 — a framebuffer GUI, networking with TLS and local inference, and
  an aarch64 port.** The ARM port is where the arch split from Chapter 1 gets its
  final exam: if the boundary was kept honest, it is a new directory, not a
  rewrite.

And underneath all of it, the stated long-term intent to **boot and install on
real hardware** — which is why the bootloader retries transient disk failures,
why the PCI scan is written to extend to bridges, why UEFI and bare-metal drivers
(AHCI, NVMe, e1000) sit on the roadmap behind the same interfaces virtio uses
today. Designing for a target you have not reached yet, at the interfaces rather
than the implementations, is the through-line.

## 17.2 How to actually get better at this

Reading a system teaches you a lot; the next order of magnitude comes from
*changing* one. [Appendix B](appendix-b-lab-book.md) turns everything below into
a full graded curriculum — diagnosis drills, reproduce-from-tests labs,
extension projects, and comparative reading — with a verification step for each.
The short version, roughly in order:

1. **Reproduce a subsystem from its tests.** Delete the body of `pmm_core.c` or
   `sched_core.c` (keep the header and the host tests) and reimplement it until the
   host tests pass. The tests are a specification; making them pass from scratch is
   how you discover which invariants are load-bearing.
2. **Add a feature with all three test levels.** Implement a new syscall — `dup`,
   `pipe`, a real `getcwd` once the VFS lands. Do it the project's way: pure logic
   host-tested, a boot marker, an invariant self-test. The discipline is the
   lesson, more than the feature.
3. **Break an invariant on purpose and watch it fail.** Remove the interrupts-off
   assertion in `schedule()`, or make `wait4` check-then-block non-atomically, and
   see what the tests catch and — more instructively — what they *don't*. This
   builds the instinct for where the sharp edges are.
4. **Take on copy-on-write fork.** The eager clone in Chapter 11 is a documented
   simplification. Implementing COW — marking shared pages read-only, handling the
   write fault, refcounting frames — teaches you the page-fault handler, the frame
   allocator, and the VMM all at once, and it is a real performance win you can
   measure.
5. **Port to real hardware.** The scariest and most educational step. Everything
   QEMU forgives, real firmware and real devices will not, and the gap *is* the
   curriculum.

## 17.3 The habits that compound

Strip away the specifics and the transferable lessons from every chapter collapse
into a handful of habits. These are what to carry into any systems work, on any
codebase:

- **Assume nothing the layer below promised in writing.** From the bootloader
  canonicalizing `CS:IP` to the kernel validating every user pointer, the bugs
  live in the assumptions you did not check.
- **Make the bug-prone logic pure, and test it with a safety net.** The single
  most repeated move in the whole project. Push hardware to the edges; put the
  arithmetic where sanitizers can reach it.
- **Complete or absent — and make the boundary loud.** Half-built features that
  return plausible wrong values are how kernels corrupt themselves. An explicit
  error at the edge of your scope is a feature.
- **State invariants in one sentence, and let the machine check them.** Vague
  confidence is where concurrency bugs breed. A checkable invariant is a claim you
  can defend.
- **Build guarantees that hold by construction.** Copy-on-write over journaling,
  unmapped page zero over null checks, W^X enforced by the MMU — prefer the
  property you get structurally over the one you have to remember to maintain.
- **Choose today's interfaces for tomorrow's requirements.** The Linux ABI, the
  arch split, the graph substrate — foresight is cheapest to buy at an interface.
- **Prove it every time, and never delete the proof.** Correctness is cumulative
  or it is temporary.

## 17.4 A closing word

The reason an operating system is the classic proving ground for a systems
programmer is not that it is the hardest code to write line by line — plenty of
application code is denser. It is that an OS gives you *no floor*. Every assumption
is yours to justify, every failure is yours to make visible, every guarantee is
yours to enforce with your own hands. There is no runtime beneath you papering
over the mistakes. That is exactly why it teaches so much: it forces the rigor that
application programming lets you skip.

You now have a complete, working example of that rigor — not as abstract advice,
but as a real system that boots, isolates processes, survives crashes, and checks
its own filesystem, with every claim in this book verifiable by opening the file
and reading the code. The path from here is not more reading. It is `make check`,
a change, and the discipline to make the machine prove your change works — and to
keep it proving so, forever. That is the whole craft. Everything else is detail.

Go build.

<div style="page-break-after: always"></div>

# Appendix A — The Folklore Margin

This appendix is for the layer you were most worried about: the tacit *why*
behind decisions that the code makes silently and the chapters state without fully
justifying. Each entry names a real decision in this codebase (with the file),
the **naive alternative** a competent-but-inexperienced engineer would reach for,
the **why** grounded in hardware, the ABI, or microarchitecture, and the
**failure** the naive choice produces. Read it alongside the chapters, or come
back to it whenever you hit a "wait, why is it done *that* way" moment.

The meta-point: none of these are in the code as comments-long-enough-to-explain,
because that is not what code is for. This is the knowledge that lives in
changelogs, mailing lists, and scar tissue. Collecting it is the whole exercise.

---

## Compiler, ABI, and the CPU's contract

### 16-byte stack alignment — even with no SSE
**Where:** `ctx.asm` (trampoline placed 8 bytes below a 16-aligned top),
`syscall_entry.asm` (exactly 16 pushes).
**Naive:** "Alignment is 2 or 8 bytes, whatever the push size is; keep the stack
wherever it lands."
**Why:** The SysV AMD64 ABI requires `%rsp` to be **16-byte aligned at the `call`
instruction** (so `%rsp+8` is a multiple of 16 on function entry, after the return
address is pushed). The rule exists so 16-byte-aligned SSE moves (`movaps`,
`movdqa`) and 16-byte types are naturally aligned. The kernel is built `-mno-sse`
and emits none of those — but the ABI is a contract the compiler's codegen assumes
*unconditionally* (stack-slot placement, any future SSE or libc-shaped code), so
the invariant is maintained anyway.
**Failure:** A misaligned stack faults or silently corrupts *inside a callee*,
arbitrarily far from the site that misaligned it — one of the most
cause-obscuring bugs there is.

### `-mno-red-zone` in the kernel
**Where:** `KERNEL_CFLAGS`.
**Naive:** "The red zone is free performance for leaf functions; leave it on."
**Why:** The ABI's 128-byte red zone below `%rsp` is safe *only* because in
userspace nothing asynchronously writes there. In the kernel, **an interrupt can
fire at any instruction**, and the CPU pushes the interrupt frame right at
`%rsp` — straight through the red zone.
**Failure:** An interrupt taken during a leaf function silently clobbers a live
local. Works for months, then destroys you under interrupt load. The flag makes
the shortcut impossible.

### `-mno-sse` / `-mno-avx` in the kernel
**Where:** `KERNEL_CFLAGS` and `USER_CFLAGS`.
**Naive:** "Let the compiler use XMM registers to copy structs; it's faster."
**Why:** The kernel does not save/restore SSE/AVX state across context switches
(lazy-FPU is deliberate later work). If the compiler used an XMM register and a
context switch landed mid-use, another thread's floating-point state is corrupted.
Forbidding SIMD makes the "we don't save FPU state" shortcut *safe* instead of a
latent corruption. (User code is `-mno-sse` for the same reason — the kernel won't
preserve its XMM state either, yet.)
**Failure:** Rare, non-deterministic FPU corruption in unrelated threads —
essentially undebuggable without knowing this is the cause.

### `CR0.WP` — the kernel obeys its own read-only bits
**Where:** `vmm_init()` (Chapter 7).
**Naive:** "Ring 0 is trusted; W^X on kernel pages is enough, no need for WP."
**Why:** By default the supervisor (ring 0) *ignores* the page writable bit — the
kernel could scribble on its own `.text`. Setting `CR0.WP` makes ring 0 honor
read-only pages too. W^X exists to contain *bugs*, and bugs are unprivileged
intent in privileged code.
**Failure:** Without WP, a wild kernel write can silently patch executable kernel
memory — the exact code-injection primitive W^X was meant to deny — and your W^X
is decorative.

### `USER_VA_LIMIT = 0x0000800000000000`, not `0x0000FFFFFFFFFFFF`
**Where:** `uaccess.c` pointer validation (Chapter 10).
**Naive:** "User space is the lower half; the limit is the top of 64-bit or the
top of the lower half's bits."
**Why:** x86-64 virtual addresses must be **canonical** — bits 63:48 must
sign-extend bit 47. The canonical *lower* half ends at `0x00007FFFFFFFFFFF`; the
next value, `0x0000800000000000`, is the first **non-canonical** address (the
start of the huge unusable hole in the middle of the space). A user pointer must
lie strictly below it.
**Failure:** Treating the address space as contiguous lets a pointer land in the
non-canonical hole; the CPU faults `#GP` (not `#PF`) on use, and validation logic
that assumed contiguity mishandles it.

---

## Concurrency (and why this kernel blocks instead of spins)

### Block/wake, not spinlocks — because it's uniprocessor
**Where:** `sched_block()`/`sched_wake()`, interrupts-off critical sections
(Chapters 5, 9).
**Naive:** "Protect shared state with a spinlock like every SMP kernel does."
**Why:** On **one CPU**, spinning to wait for a lock held by another thread is a
*deadlock* — the holder cannot run while you spin. So mutual exclusion is
"interrupts off" (nothing else can run at all), and waiting for an *event* is
blocking (`sched_block`) until a `sched_wake`. On SMP you would add real
spinlocks — and then the classic cost appears: the lock's cache line **bounces**
between cores (every failed CAS invalidates it in all other caches via MESI),
making contention super-linear. The rule then: spin only if the expected wait is
shorter than a context switch (~thousands of cycles), else block; real locks do
both (adaptive).
**Failure:** A naive spinlock on this uniprocessor kernel deadlocks the first time
a thread waits on another. A naive spinlock on future SMP with no backoff melts
under contention from cache-line bouncing.

### `cpu_irq_save`/`restore` nest (save previous state, never unconditionally enable)
**Where:** the interrupts-off lock, used by the heap, PMM, scheduler.
**Naive:** "Disable at the start of the critical section, `sti` at the end."
**Why:** Kernel code is reentrant — an allocator calls another allocator, a locked
subsystem allocates under its own lock. If the inner unlock did an unconditional
`sti`, it would re-enable interrupts while the *outer* critical section still
needed them off. Save-and-restore of the prior `IF` state composes correctly.
**Failure:** A one-instruction interrupt window opens every time the lock is used
reentrantly — a race that fires only under the exact nesting-plus-interrupt
timing, i.e. almost never in testing and constantly in production.

### Preempt at IRQ exit *after* EOI — not inline in the tick handler
**Where:** `sched_preempt()` at IRQ exit (Chapter 9).
**Naive:** "The timer decided to reschedule, so call `schedule()` right here in
the handler."
**Why:** The context switch must happen after the interrupt is fully retired and
the EOI sent, or the PIC still thinks that IRQ is in service and can stall further
ticks; and switching mid-handler runs another thread on a half-serviced interrupt.
Deferring to IRQ exit lets the preempted thread's frame park on its own stack and
resume later through the normal interrupt-return path, none the wiser.
**Failure:** Switching inline can wedge the timer (no more ticks → no more
preemption) and tangles the interrupt-return state.

### The joiner frees the zombie's stack, never the exiting thread
**Where:** `thread_join` / zombie reaping (Chapter 9, invariant 5).
**Naive:** "A thread cleans up its own stack when it exits."
**Why:** A thread's stack is *in use right up to its final `ctx_switch`* — the
switch itself executes on it. The exiting thread therefore cannot free the stack
it is standing on. The one party guaranteed to run strictly after that final
switch is whoever `join`s it, so freeing is the joiner's job.
**Failure:** Freeing your own stack is a use-after-free of live execution state —
the worst class of bug, and it "works" until the freed frame is reused between the
free and the switch.

---

## Memory management

### PMM: mark everything used, *then* free the usable ranges
**Where:** `pmm_init()` construction order (Chapter 6).
**Naive:** "Mark everything free, then carve out the reserved/MMIO ranges."
**Why:** If you start from "all free" and forget to carve out one reserved range,
you hand that frame to the heap — but it's actually MMIO or ACPI tables, and the
corruption is silent and late. Start from "all used" and a forgotten range simply
stays unavailable.
**Failure:** The two failure modes are "too conservative" (out-of-memory: loud,
early, obvious) versus "silent corruption" (late, catastrophic). Choose the init
order that makes your bug land on the conservative side.

### 2 MiB huge pages for the direct map, 4 KiB for the kernel image
**Where:** `vmm_init()` (Chapter 7).
**Naive:** "Map everything at 4 KiB; it's uniform and simple."
**Why:** The direct map covers all of RAM; at 4 KiB that is megabytes of page
tables and enormous TLB pressure. A 2 MiB page is one TLB entry covering 512× the
range ("TLB reach"), and the direct map needs no fine-grained permissions. The
kernel *image*, by contrast, needs 4 KiB granularity so W^X boundaries land
exactly on section edges.
**Failure:** All-4 KiB wastes memory on page tables and thrashes the TLB;
all-2 MiB cannot enforce W^X at section granularity (a 2 MiB page is one
permission for 2 MiB of mixed code and data).

### Leave page zero unmapped on purpose
**Where:** the address-space layout; the ELF loader refuses to map the null page.
**Naive:** "Add explicit null checks where pointers might be null."
**Why:** Leaving virtual page 0 unmapped turns *every* null dereference — in the
kernel or a process — into a clean page fault, for free, with no per-site checks.
It is the one piece of the userspace safety net you get to keep in ring 0.
**Failure:** Map page zero (or don't reserve it) and a null deref reads/writes
real memory silently instead of faulting.

---

## Storage and the filesystem

### 4 KiB block = page = filesystem block
**Where:** the block layer (Chapter 12), graphfs (Chapter 13).
**Naive:** "The device sector is 512 bytes; use that as the block size."
**Why:** Making the block layer's block, the MMU's page, and the filesystem's
block all the same 4 KiB size means one unit crosses every boundary — no
conversion arithmetic between layers, and a filesystem block can be mapped as a
page directly.
**Failure:** Mismatched granularities breed off-by-a-factor bugs at every layer
boundary and block zero-copy paths that want page-sized units.

### Write-through cache, not write-back
**Where:** the block layer's LRU cache (Chapter 12).
**Naive:** "Write-back is faster — batch dirty blocks and flush later."
**Why:** graphfs's crash-consistency (below) depends on "once acknowledged,
durable." Write-back keeps acknowledged data only in RAM until a later flush, so a
crash loses it and breaks the filesystem's atomicity guarantee. Write-through
trades throughput for the durability the layer above requires.
**Failure:** Write-back under a copy-on-write filesystem means a crash can lose a
superblock write that the filesystem believed committed — corruption of the exact
data the CoW design worked to protect.

### Poll for virtio-blk completion (v1), don't sleep on an interrupt
**Where:** `virtio_blk.c` (Chapter 12).
**Naive:** "Interrupt-driven I/O is the 'real' way; polling is a toy."
**Why:** v1 has a single synchronous requester with no concurrent I/O. Polling
with a bounded timeout is *completely* implemented and simple; interrupt-driven
completion only pays off with multiple in-flight requests, which arrives with the
VFS. Complete-or-absent: ship the simple thing fully rather than the complex thing
partially.
**Failure:** Premature interrupt-driven concurrency with a single requester adds
race surface and completion-matching bugs for zero throughput benefit.

### Checksum lives in the *pointer*, not in the block
**Where:** `struct gfs_bp { phys, crc }` (Chapter 13).
**Naive:** "Store each block's checksum inside the block."
**Why:** A self-checksum can't detect a **misdirected write** (right bytes, wrong
address — a real hardware failure): the block carries a valid checksum for its own
contents. Putting the checksum in the parent pointer forms a self-validating tree
— you trust a block only via a parent that both located and attested it — catching
misdirected and torn writes on read.
**Failure:** In-block checksums silently accept a misdirected or half-written
block that happens to be internally consistent.

### Copy-on-write commit (double superblock), not a journal
**Where:** two superblock slots by `generation & 1`, double-buffered bitmap
(Chapter 13).
**Naive:** "Do crash consistency the standard way: a write-ahead journal."
**Why:** A journal writes metadata twice and needs replay-on-recovery. CoW never
overwrites live data and flips one atomic superblock write, so an interrupted
commit is *structurally* a no-op — consistency by construction, no journal, no
repair-on-boot fsck. The two superblock slots exist so a commit never touches the
live one.
**Failure:** In-place updates (journal or not) create windows where a crash leaves
half-applied state; getting journal replay exactly right under every crash point
is itself a notorious source of bugs.

### Eager fork, not copy-on-write (for now) — and why that's honest
**Where:** `paging_user_clone` (Chapter 11).
**Naive:** "Everyone does COW fork; do it now."
**Why:** COW fork needs the page-fault handler, frame refcounting, and read-only
downgrade of shared pages — a real subsystem. Rather than a half-built COW that
subtly mishandles a refcount, v1 copies eagerly (correct, slower) and documents
COW as a bounded future optimization. This is complete-or-absent: the shortcut is
*correct*, just not optimal, and its cost and lift path are written down.
**Failure:** A half-implemented COW that drops a refcount frees a page another
process still maps — silent cross-process corruption, far worse than "fork copies
too much."

---

## How to extend this margin

When you study a decision and can articulate its naive alternative and failure
mode, add it here in the same shape. That act — forcing yourself to name the bug
the design avoids — is the single most efficient way to convert code you can
*read* into judgment you can *reuse*. When you cannot work out the why, that is
exactly the question to bring to a review or to me: chase it to the hardware, the
ABI, or the changelog, and write down what you find.

<div style="page-break-after: always"></div>

# Appendix B — The Lab Book

Reading made you a sharp reader of this code. This appendix is what makes you a
builder, because the difference between the two is repetitions: hours spent
changing a kernel, breaking it, watching it fail, and fixing it with the
instruments from Chapter 0. Every lab below is concrete, runs against this
repository, and ends with a verification step — because "done" here means the
machine demonstrates it (Chapter 15), including for your homework.

**Difficulty grades:**

- ★ — observation labs: run and watch (an evening)
- ★★ — modification labs: change existing code (a day or two)
- ★★★ — construction labs: build something real (a week-ish)
- ★★★★ — capstones (multiple weeks; these are the ones that change you)

**Rules for every lab:** work on a branch (`git switch -c lab/<name>`), keep
`make check` green before you call anything done, and hold yourself to the
book's standards — pure logic host-tested, boot-visible behavior gets a marker,
`-Werror` stays on. Doing the labs *the project's way* is half of what they
teach.

---

## B.1 Diagnosis drills — break it, predict, observe, restore

These train the debugging instinct directly. The protocol matters more than the
drill: **(1)** make the one-line sabotage, **(2)** *write down your prediction* —
what will fail, and what each instrument will show, **(3)** boot and observe with
the named instrument, **(4)** compare prediction to reality (the gap is the
lesson), **(5)** `git checkout .` and move on. Never skip step 2 — prediction is
the exercise; observation alone teaches nothing.

Each drill names its chapter and its instrument.

1. **Remove the `sti` in `thread_entry_trampoline`** (`ctx.asm`; Ch. 9).
   Instrument: serial output + your watch. Predict what happens to preemption and
   which selftest hangs or fails, and why every thread after the first inherits
   interrupts-off.
2. **Remove one `push`/`pop` pair from `syscall_entry.asm`** (Ch. 10). Instrument:
   init's exit status. Predict which of init's twenty checks catches it (hint:
   the sentinel-register test) and what the misaligned frame does to
   `struct syscall_frame`'s field mapping.
3. **Skip the EOI for the timer IRQ** (Ch. 5). Instrument: serial. Predict
   exactly how far the boot gets and why the system freezes where it does (one
   tick is serviced, then never again).
4. **Break the DAP sector count patch** — have `mkimage.py` write 1 instead of
   the real stage-2 count (Ch. 2–3). Instrument: serial. Predict which stage-1
   `ERR:` message you get (magic check catches the truncated load).
5. **Swap the order of `EFER.LME` and `CR0.PG` in stage 2** (Ch. 3). Instrument:
   QEMU `-d int,cpu_reset -no-reboot`. Predict the fault vector. This is the
   canonical triple-fault drill — do it once and `-d int` output will never
   intimidate you again.
6. **Map the kernel's `.text` writable** in `vmm_init` (Ch. 7). Instrument: the
   boot selftests. Predict which assertion fails. Then also clear `CR0.WP` and
   predict what changes (Appendix A's WP entry, demonstrated).
7. **Make `wait4`'s zombie-check and block non-atomic** — re-enable interrupts
   between them (Ch. 11). Instrument: repeated `make check-boot` runs. This is a
   *race*: predict why it passes most runs and how the lost wakeup manifests
   when it doesn't. Losing a race intermittently, on purpose, is the best
   concurrency education available on one CPU.
8. **Corrupt one byte of a graphfs metadata block** — `dd` a byte into
   `build/fs.img` after `make`, then run `build/graphfs_fsck` (Ch. 13). Predict
   which checksum in the self-validating tree catches it, then corrupt a *data*
   block and explain why v1 fsck does not (metadata-only checksums — the
   documented limit, observed).
9. **Return the wrong value from an unimplemented syscall** — `0` instead of
   `-ENOSYS` (Ch. 1, 10). Instrument: init's exit status. Predict which check
   fails and reflect on what a *silent* plausible value would have done if init
   didn't test for it.

## B.2 Reproduce-from-tests labs — the flagship exercises

The pure cores plus their host tests form executable specifications. Blank the
implementation, keep the header and tests, and reimplement until
`make check-host` is green — then `make check` to prove the kernel still boots on
*your* implementation. Do them in this order; each is bigger than the last:

1. ★★ **`kernel/lib/fmt.c`** — reimplement `vsnprintf` against `test_fmt.c`.
   Bounded formatting is a perfect first rep: pure, fiddly, and the tests are
   merciless about edge cases.
2. ★★ **`kernel/mm/pmm_core.c`** — the frame bitmap, against `test_pmm.c`.
   You will meet every off-by-one Chapter 6 warned about.
3. ★★★ **`kernel/sched/sched_core.c`** — ready queue and sleep list, against
   `test_sched.c` including the 20,000-round randomized stress run. Passing a
   shadow-model stress test with your own list code is a rite of passage.
4. ★★★ **`kernel/lib/elf64.c`** — the validator, against `test_elf64.c`'s
   one-mutation-per-rejection-path suite. This teaches total validation of
   hostile input better than any prose.
5. ★★★★ **`kernel/fs/graphfs_core.c`** — the whole filesystem core, against
   `test_graphfs.c` and `make check-fsck`. The final exam of the pattern: if
   your reimplementation passes fsck and boots the kernel, you have written a
   crash-consistent filesystem to a fixed on-disk spec.

When you finish one, diff yours against the original. Every place the original
does something you didn't is either a bug you have (find it) or a judgment call
you haven't learned yet (chase it — Appendix A style).

## B.3 Extension labs — build features the project's way

Each of these adds something real. The deliverable is not just the feature: it's
the feature **plus** its host tests, its boot marker or selftest, and its docs
paragraph — the full Chapter 15 definition of done.

1. ★★ **A new syscall: `uname` (63)** — return a hardcoded
   `struct utsname` through a validated user pointer. Small, but it walks the
   entire path: dispatch table, uaccess copy-out, a user-side check in init.
2. ★★ **Kernel-stack guard pages** — map an unmapped page below each thread's
   kernel stack, and prove a stack overflow now produces the IST double-fault
   diagnostic instead of silent corruption (Ch. 4's investment, completed).
3. ★★ **A block-cache hit-rate counter** — count hits/misses in
   `kernel/block/block.c`, print the ratio at boot, and watch it change as you
   vary the fsck workload. Your first mechanical-sympathy *measurement*.
4. ★★★ **`WNOHANG` for `wait4`** — a documented limit, lifted properly: table
   logic in `proc_core.c` (host-tested), the flag in the syscall, an init check
   for both the "no zombie yet" and "zombie ready" cases.
5. ★★★ **Interrupt-driven virtio-blk completion** — replace polling with
   sleep-on-IRQ using `sched_block`/`sched_wake` (Ch. 5's producer/consumer
   shape, applied to a real device). Keep the bounded timeout as a watchdog.
6. ★★★★ **Copy-on-write fork** — the canonical capstone. Read-only shared
   pages, frame refcounts in the PMM, the write-fault path in the page-fault
   handler, and the eager-copy fallback removed. Prove it with init's existing
   fork tests *plus* a new frame-count assertion showing fork no longer copies
   the whole image. Touches Ch. 6, 7, 10, 11 at once — that's why it's the one.
7. ★★★★ **A VFS slice** (or: do Phase 5c before the project does) — fd tables,
   `open`/`read`/`close` over graphfs, exec-from-disk. If the project has
   already landed 5c by the time you read this, reimplement it on a branch
   without looking, then compare.

## B.4 Comparative reading — how pros study other kernels

Professionals read other people's kernels the way writers read other people's
books. The assignments below pair something you now understand deeply with the
same mechanism elsewhere; write a half-page comparison for each (what differs,
*why* — hardware, era, scale, or taste):

1. ★ **xv6-riscv** (`github.com/mit-pdos/xv6-riscv`, ~6k lines, free book) —
   read `swtch.S` against `ctx.asm` (same trick, different ISA), `trap.c`
   against `trap.c` + `syscall_entry.asm` (RISC-V has no
   SYSCALL/SYSRET split — what replaces it?), and `vm.c` against `vmm.c`
   (Sv39's three levels vs x86's four).
2. ★★ **Linux, targeted files only** — never "read Linux," read *one mechanism*:
   `kernel/sched/core.c`'s `__schedule()` against `sched.c` (what does a
   scheduler need at 10 orders of magnitude more scale?);
   `arch/x86/entry/entry_64.S` against `syscall_entry.asm` (find the `swapgs`
   this book said arrives with SMP); `fs/namei.c`'s path walk against
   `gfs_resolve` (RCU-walk is the answer to a question this kernel doesn't have
   yet — what question?).
3. ★★ **The historical designs behind Chapter 13** — the ZFS on-disk
   specification (checksummed block pointers) and the btrfs wiki's design pages
   (CoW B-trees). graphfs made different simplifications than both; name three
   and the workload where each would bite.
4. ★ **OSTEP** (*Operating Systems: Three Easy Pieces*, free) — not for the
   mechanisms (you now know them concretely) but for the scheduling-policy and
   concurrency chapters, which cover the *policy* space this kernel's
   round-robin deliberately skips.

## B.5 When the missing layers become real

Be honest about what this repo cannot teach (Appendix A said it; the lab book
repeats it): **SMP, memory ordering, and real performance work need hardware
this project doesn't exercise yet.** The trigger points, so you know when to go
get them:

- The day this kernel boots a second CPU, the interrupts-off lock becomes a
  spinlock, `swapgs`/per-CPU state replaces the `syscall_kstack` global, and
  McKenney's perfbook plus SDM Vol. 3A Ch. 8 stop being background reading and
  become the spec you're implementing against.
- The day it boots on real hardware, Gregg's measurement methodology replaces
  QEMU-TCG intuition — emulated timing lies to you, and every performance
  belief you formed under TCG must be re-verified.

Until then: the drills build your debugging reflexes, the reproductions build
your implementation muscle, the extensions build your judgment, and the
comparative reading builds your taste. That combination — not any book,
including this one — is what a professional OS developer is made of.

Keep a log as you go: for every lab, what you predicted, what happened, and the
one thing you didn't expect. Six months of that log *is* your expertise, written
down.

<div style="page-break-after: always"></div>

# Appendix C — Bug Hunts: Three Real Bugs, End to End

Every chapter tells you *what* the debug loop is. This appendix shows you what
it feels like to run it. Three real bugs from this codebase's own history —
each one attested in the commit record — walked end to end: the symptom (or
the eerie absence of one), the chase, the fix, and the regression test that
makes the bug unrepeatable. The narrative of each chase is reconstructed from
the commits, the code, and the way the instruments behave; the bugs, the
fixes, and the proofs are exactly as the record states them.

Read these the way you would read annotated chess games. The point is not the
specific bugs — you will never hit these three exactly — it is the *move
order*: what the debugger looked at first, which instrument answered which
question, and where the hunt actually ended (hint: never at "it works now").

The appendix ends with the **triage table**: symptom → first suspicion →
first instrument, for the dozen failure shapes that cover most of what bare
metal will throw at you.

---

## C.1 The bug that produced no symptom: the syscall register clobber

**The record:** commit `a58bbe1`, "syscall: preserve all caller-saved user
registers across the entry," landed mid-Phase 4, between the first C
userspace program and fork.

### The setting

By this point the kernel had a working `SYSCALL`/`SYSRET` path. The entry
stub parked the user stack pointer, adopted the kernel stack, saved the three
registers the hardware itself involves — `rsp`, `rcx` (return RIP), `r11`
(RFLAGS) — and called the C dispatcher. Every test was green: fifteen serial
markers, the C init printing from ring 3, exit status 0. Nothing was wrong.

That sentence should already bother you. "Every test was green" is a
statement about the tests, not about the code.

### The chase

This hunt did not start with a crash. It started with someone reading the
entry stub side by side with the contract it claims to implement — the Linux
x86_64 syscall ABI, which this kernel adopted deliberately (Chapter 10). The
contract says: across a syscall, userspace sees only `rax` (the result),
`rcx`, and `r11` (hardware-clobbered) change. **Every other register is
promised back intact.**

Now look at what the stub actually preserved: `rsp`, `rcx`, `r11`. And ask
the question that cracks it open: *who else touches registers between
`syscall` and `sysret`?* The C dispatcher does — and the System V ABI
entitles any C function to trash every caller-saved register:
`rdi, rsi, rdx, r10 (as rcx), r8, r9`, and more. The stub was relying on the
C dispatch to happen not to clobber the six argument registers.

So why was everything green? Because init's syscall wrappers listed
`rcx`/`r11` as clobbers and the compiler — so far, by pure register-allocation
luck — had never kept a live value in `rdi` through `r9` across a `syscall`
instruction. The commit message names the blast radius precisely: userspace
was "one register-allocation decision away from silent corruption."

Dwell on what the symptom *would* have been, because this is the shape of
bug that costs a week: some future program's local variable changes value
across an innocent `write()`. No fault. No panic. The kernel is fine —
it is technically doing everything its tests assert. The corruption appears
only with the compiler versions and optimization levels that happen to keep
a value live in the wrong register, and it moves when you add a `kprintf`.
Debugging that from the symptom end is archaeology. Finding it from the
contract end was one careful read.

### The fix

Mechanically small: push the full caller-saved set on entry, pop it before
`sysret`, keep `rsp` 16-aligned at the `call` (ten pushes at the time). The
interesting part is what the fix *became*. One slice later, fork needed the
complete user register state to clone a process — and the natural
implementation was to widen this same frame to all fifteen GPRs. Look at
`syscall_entry.asm` today: the entry builds a complete `struct
syscall_frame`, the dispatcher gets a pointer to it, and `fork()` clones a
user context by copying that struct. The bug fix's shape turned out to be
the foundation of the next feature. That is not luck; honoring the full
contract usually *is* the design the next feature needs.

### The proof

Rule 2 of the testing policy (Chapter 15): a bug fix lands with a test that
failed before it. The test here is worth studying because getting it right
is subtle — you are trying to catch the *compiler* being entitled to hurt
you, using the compiler:

```c
static int regs_survive_syscall(void) {
    long rdi = 0x1111, rsi = 0x2222, rdx = 0x3333;
    register long r10 __asm__("r10") = 0x4444;
    register long r8  __asm__("r8")  = 0x5555;
    register long r9  __asm__("r9")  = 0x6666;
    long ret;
    __asm__ volatile("syscall"
                     : "=a"(ret), "+D"(rdi), "+S"(rsi), "+d"(rdx),
                       "+r"(r10), "+r"(r8), "+r"(r9)
                     : "a"((long)SYS_getpid)
                     : "rcx", "r11", "memory");
    /* ...verify all six sentinels... */
```

The `"+"` constraints are the whole trick: they force the compiler to treat
each register as an *output* too — to read reality back after the `syscall`
instead of assuming the asm preserved its inputs. Sentinels in all six
argument registers, checked after. The commit records the verification both
ways: against the old stub, init exits with status 10 and the boot test
fails; with the fix, all fifteen markers pass.

### What to keep

- **Audit entry paths against the contract as written, not against "it
  works."** Green tests measure what the tests assert, and a latent contract
  violation asserts nothing until the day it corrupts something.
- **The best time to find a bug is before it has a symptom.** The
  contract-vs-code read is a debugging instrument too — arguably the highest-
  yield one, and the only one that works on bugs that haven't fired yet.
- **When you fix a contract bug, fix the written contract too.** The same
  commit added a "preserved" row to Appendix H's ABI table. The next
  reader of the stub now has the promise in front of them.

---

## C.2 The pointer that outlived its address space: the PMM bitmap and the HHDM flip

**The record:** commit `0c89e13`, "mm: kernel page tables," which states:
"The boot test caught two real bugs during bring-up: the direct-map ceiling
following a reserved 1 TiB PCI hole, and the PMM bitmap pointer going stale
across the hhdm_base flip." This hunt is the second one; §C.3 is the first.

### The setting

Chapter 7's moment of maximum danger: `vmm_init()` builds the kernel's real
page tables and switches `CR3` away from the bootloader's throwaway
mappings. Before the switch, physical memory is reachable through the boot
alias; after it, through the brand-new higher-half direct map at
`0xffff800000000000` — and the old identity map is *gone, on purpose* (an
absent mapping is a null-pointer trap; Chapter 7 §7.2). The kernel's
`phys_to_virt()` reads a runtime `hhdm_base` that `vmm_init()` flips from
the boot alias to the real HHDM at the moment of the switch.

The physical-memory allocator, brought up one commit earlier, keeps its
bitmap in a frame it chose at init time — and it reaches that bitmap through
a **virtual pointer it computed once, at init, under the old mapping**.

You can see it now. The whole point of a hunt narrative, though, is that at
the time, nobody could.

### The chase

The symptom: boot proceeds normally — `pmm:` marker, then
`vmm: kernel page tables active` — and then dies in a page-fault panic the
next time anything allocates a frame. The integration test fails on a
missing marker with a `PANIC` line in the transcript.

Move one: **read the panic.** This same commit had just built the decoding
page-fault handler — the one that prints access type, privilege, and `CR2`
before dying (Chapter 7 §7.4) — and this bug is the reason that investment
pays back immediately. The dump says: write, ring 0, to a low-ish virtual
address that is *not* in the higher half and *not* in the new direct map.

Move two: **ask where that address came from.** It is not garbage — it is
suspiciously well-formed, exactly where the PMM's bitmap used to live under
the boot-era mapping. The faulting RIP is inside the frame allocator. So the
allocator is writing to its bitmap through a pointer that was true fifteen
microseconds ago, under an address-space regime that no longer exists.

Move three: generalize before fixing. The question is not "how do I fix the
PMM" but **"who else cached a translation across the flip?"** Grep every
call site that stores the result of `phys_to_virt()` (or any boot-era
virtual address) into a long-lived variable. The audit found the same
disease in the VGA driver, which had cached its aperture pointer.

### The fix

Two fixes, matched to how each caller uses its pointer. The PMM gets an
explicit `pmm_rebase()` call — `vmm_init()` tells it "the world moved,
re-derive your bitmap pointer" — which is honest about the dependency:
the *ordering* between the VMM flip and the rebase is now visible in
`vmm_init`'s code instead of implicit in a stale pointer. The VGA driver
stops caching entirely and resolves its aperture through `phys_to_virt()`
on every call — correct by construction, at a cost that is irrelevant for
a text console. Chapter 6 §6.3 tells the PMM half of this story as a design
pattern; this is the bug that made it one.

### The proof

The boot selftests added in the same commit assert the new regime directly:
HHDM translation is verified, the old identity map is verified *gone*, and
the PMM's frame alloc/write/free selftest — which runs after `vmm_init` —
now exercises the rebased pointer on every boot. The bug cannot return
silently, because the very next boot repeats the exact sequence that
exposed it.

### What to keep

- **A cached translation is a bet that the mapping regime never changes.**
  Every pointer you store is implicitly `virt_of(phys, mapping_epoch)`. When
  an init step changes the epoch — enabling paging, moving to a new CR3,
  tearing down a boot alias — every stored translation is suspect. Audit
  them *as part of designing the transition*, not after the panic.
- **When one caller has a staleness bug, grep for the whole species.** The
  second infection (VGA) was found by audit, not by symptom. One instance of
  a bug is a claim about the pattern, not just the site.
- **Build the decoder before the fault.** The reason this hunt took minutes
  and not hours is that the page-fault handler printed `CR2` and the access
  type from day one. Diagnostics built at the choke point are an investment
  that pays on the *first* bug after them — which, here, was the same commit.

---

## C.3 The machine that told the truth strangely: the 1 TiB direct-map ceiling

**The record:** the first of `0c89e13`'s two boot-test catches: "the
direct-map ceiling following a reserved 1 TiB PCI hole."

### The setting

`vmm_init()` must decide how much physical address space the direct map
covers. The natural implementation reads the E820 map and covers everything
up to the highest address any entry mentions — one loop, obviously correct.

Then the boot test ran against QEMU's actual E820 map, which contains a
**reserved** entry parked around the 1 TiB mark — a 64-bit PCI MMIO hole,
a completely legitimate thing for firmware to report. The "obviously
correct" ceiling followed it.

### The chase

The symptom is instructive because nothing about it says "PCI hole": the
boot test flags the run, and the numbers in the transcript are quietly
absurd — the `pmm:` marker reports ~255 MiB of RAM, and `vmm_init` is
building a direct map three orders of magnitude larger.

Move one: **make the invisible loop visible.** A `kprintf` of the computed
ceiling before the mapping loop is one line, and it prints a number with
twelve zeros. There is the bug, in your own output: the kernel is dutifully
building 2 MiB mappings for a *terabyte* of address space that contains a
few hundred MiB of RAM — over a thousand page-table frames, megabytes of a
small machine's memory and a flood of TLB-hostile mappings spent describing
nothing. On a machine with a bigger hole, or a design mapping at 4 KiB
granularity (where the same tables cost five hundred times more), this
graduates from waste to frame exhaustion mid-`vmm_init`.

Move two: interrogate the assumption. The E820 map is not "the RAM map" —
it is the firmware's report of *everything it knows about the physical
address space*, and reserved entries can sit anywhere, including absurdly
high. The direct map's job is to reach **RAM** (plus the low MMIO window the
kernel actually uses). The ceiling should follow the highest *usable* entry,
not the highest entry.

### The fix

The direct map covers all usable RAM plus the first 4 GiB (the legacy/PCI
MMIO window), explicitly — the reserved outlier no longer participates in
the ceiling. The distinction is even visible in cache attributes: RAM is
mapped write-back, the non-RAM window cache-disabled (Chapter 7).

### The proof

The boot selftests verify HHDM translation against the real map, and the
`pmm:`/`vmm:` markers — asserted on every boot — pin the observable
behavior: the frame budget survives `vmm_init` with hundreds of MiB free.
A regression toward "map everything E820 mentions" would blow the frame
accounting the very next selftest checks.

### What to keep

- **Firmware tables describe the address space, not your memory.** Reserved
  entries at absurd addresses are not malformed input; they are the normal
  weather of real machines. Any loop over a firmware table needs to decide,
  explicitly, which *kinds* of entry drive which decision.
- **When a loop misbehaves, print its bounds before stepping through its
  body.** One `kprintf` of the ceiling turned a mystery hang into an
  arithmetic error you could read off the screen.
- **This is why the integration test runs on every commit.** Nothing about
  the ceiling logic looks wrong on a whiteboard; it is wrong against one
  particular machine's E820 map. Bugs like this are found by *booting*,
  which is exactly what `make check` refuses to let you skip.

---

## C.4 The triage table

The three hunts above, and every drill in Appendix B, run on the same first
move: **classify the symptom, then reach for the instrument that classifies
it further.** This table is that first move, precomputed. It assumes the
codebase's own discipline — every fatal path loud (`PANIC`/`ERR:`), the
decoding fault handlers installed — which is what makes the symptoms
distinguishable at all. (Instruments: Chapter 0 §0.5.)

| Symptom | First suspicion | First instrument |
|---|---|---|
| Silent hang, no output at all | Wedged before a loud failure point: early spin loop, fault before the IDT exists, deadlock with interrupts off | `kprintf` bisection toward the last line printed; then `-d int` |
| Instant reboot, or a reboot loop | Triple fault: a fault whose handler faulted | `-d int -no-reboot -no-shutdown`; read the **first** exception in the log, not the last |
| `PANIC` with a register dump | The kernel caught its own bug — the easy case | Read file:line and the dump; map RIP to source with gdb/`addr2line` on `kernel.elf` |
| `#PF` panic | Bad pointer — but *whose*? | Read `CR2` and the error-code bits (present/write/user/fetch); then ask "who computed this address," not "who dereferenced it" |
| `#GP` panic | Error code ≠ 0: a segment selector is involved (IDT/GDT entry, `iretq` frame). Error code 0: non-canonical address, privilege violation, or a bad MSR write | Decode the error code against SDM Vol. 3A Ch. 6.15; inspect the frame you were returning through |
| `#UD` panic | Execution landed somewhere that is not code | Is RIP inside `.text`? Yes → a deliberate `ud2`/assert. No → wild jump: corrupted function pointer or smashed return address |
| `#DF` on the IST stack | Kernel stack overflow, or a fault taken while delivering a fault | Check saved RSP against the thread's stack bounds; thank the IST slot (Chapter 4) for the diagnostic |
| Serial garbled or absent, VGA fine | UART init or baud divisor; or serial self-disabled after a wedged transmitter | Loopback self-test result; simplify to `-serial stdio` and retest |
| Wrong values, no fault, kernel healthy | Memory corruption or a violated contract (DMA landing wrong, uaccess miss, register clobber — see §C.1) | Move the logic into a host test under ASan/UBSan; on metal, bisect with `KASSERT`s of the nearest invariant |
| Green on host tests, dies on metal | Codegen/environment gap: code model, no-SSE, red zone, alignment, real page tables | Level 2 territory — reproduce in `selftest.c` under the real toolchain flags |
| Passes `make run`, fails `make check` | Timing, or marker order/wording drift | Diff the harness transcript against the expected marker list |
| Appears/vanishes with unrelated edits | Layout-sensitive latent bug: uninitialized memory, stack overflow, misalignment | Stop chasing the trigger; hunt the sensitivity — sanitizers on the nearest pure core, stack canary checks |

Two rules complete the table. First, **silence is data**: in a codebase
where every failure is loud, the absence of output localizes the failure to
the regions that cannot yet speak (Chapter 15 §15.3). Second, **the last
move of every hunt is a test** — the table gets you to a cause, but the hunt
ends only when the cause cannot recur unannounced. That is the difference
between "fixed it" and "finished it," and it is the through-line of all
three stories above.

<div style="page-break-after: always"></div>

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

<div style="page-break-after: always"></div>

# Boot Protocol (version 1)

This document is the contract between the Hallucinate OS bootloader and the kernel. It is
versioned; any incompatible change bumps `BOOTINFO_VERSION` in `kernel/include/bootinfo.h`
and this document together.

## Disk layout

The system boots from a raw disk image assembled by `tools/mkimage.py`:

| LBA | Contents |
|---|---|
| 0 | Stage 1 (MBR, exactly 512 bytes, `0xAA55` signature) |
| 1 .. N | Stage 2, sector-padded (N ≤ 127 so stage 1 can load it in one INT 13h read) |
| N+1 .. | Kernel ELF64 image, sector-padded |

The image is zero-padded to a whole MiB.

### Build-time patching

Neither stage hardcodes the image geometry. `mkimage.py` locates unique markers in the
assembled binaries and patches them:

- **Stage 1**: marker `"HB1\0"` immediately precedes its Disk Address Packet. The DAP
  sector-count word (marker offset +6) receives the stage-2 sector count. `mkimage`
  verifies the DAP header bytes (`0x10 0x00`) follow the marker.
- **Stage 2**: marker `"HB2\0"`; offset +4 holds the kernel start LBA (u64), offset +12
  the kernel sector count (u32).

## Stage 1 (`boot/stage1.asm`)

Loaded by the BIOS at `0x7C00` with the boot drive number in `DL`.

1. Canonicalizes `CS:IP` via a far jump, zeroes segment registers, sets the stack just
   below `0x7C00`.
2. Verifies INT 13h extensions (`AH=41h`, signature `0x55AA`); LBA reads are required.
3. Reads stage 2 to `0x7E00` with INT 13h `AH=42h`, retrying up to 3 times with a disk
   reset between attempts.
4. Verifies the first dword of stage 2 is `0x32534C48` (`"HLS2"`).
5. Far-jumps to `0:0x7E04` with `DL` still holding the boot drive.

On any failure it prints an `ERR:`-prefixed message and halts. The integration test
harness treats `ERR:` on the serial console as fatal.

## Stage 2 (`boot/stage2.asm`)

Entered in 16-bit real mode at `0x7E04`.

1. **A20**: tests for wraparound at `0x100500`; if disabled, tries INT 15h `AX=2401h`,
   then port `0x92` (fast A20), then the 8042 keyboard controller. All KBC waits are
   bounded (64K polls). Failure is fatal.
2. **E820**: walks INT 15h `AX=E820h`, storing up to 64 raw 24-byte entries directly into
   the bootinfo block. The ACPI extended-attributes dword is preset to 1 before each call
   so 20-byte BIOS replies remain valid. At least one entry is required.
3. **Kernel load**: reads the kernel image in 64-sector (32 KiB) chunks to a real-mode
   buffer at `0x20000`, then copies each chunk to the staging area at `0x1000000` (16 MiB)
   using unreal mode (`a32 rep movsd` with interrupts disabled). Unreal mode is re-entered
   after every INT 13h call because the BIOS may reset cached descriptor limits. Reads
   retry 3 times.
4. **Protected mode**: loads a GDT (null, 32-bit code `0x08`, data `0x10`, 64-bit code
   `0x18`) and enters 32-bit protected mode.
5. **Paging**: builds 4-level page tables at `0x70000`–`0x73FFF` mapping the first 1 GiB
   of physical memory twice with 2 MiB pages: identity (`PML4[0]`) and at the kernel VMA
   (`PML4[511] → PDPT[510]`, i.e. `0xffffffff80000000`). Both mappings share one page
   directory.
6. **Long mode**: sets CR4.PAE, loads CR3, sets EFER.LME (MSR `0xC0000080` bit 8), enables
   paging, and far-jumps to 64-bit code.
7. **ELF load** (64-bit): validates the staged image (ELF magic, 64-bit, little-endian,
   `EM_X86_64`) and for each `PT_LOAD` segment checks `p_filesz ≤ p_memsz`,
   `p_paddr ≥ 0x100000`, and that the segment ends below the staging area; then copies
   file bytes to `p_paddr` and zeroes BSS. The entry point must be ≥
   `0xFFFFFF8000000000`.
8. Jumps to the ELF entry point with `RDI` = physical address of the bootinfo block.

Errors in 64-bit mode (no BIOS available) are reported white-on-red via the VGA text
buffer at `0xB8000`.

## Kernel entry state

At `_start` (`kernel/arch/x86_64/entry.asm`):

- CPU in 64-bit long mode, interrupts disabled, paging per step 5 above.
- `RDI` = physical address of the bootinfo block (`0x6000`).
- GDT is stage 2's; the kernel must install its own before relying on selectors.
- No stack guarantee: the kernel entry stub installs its own 16 KiB stack.
- All kernel `PT_LOAD` segments copied and BSS zeroed by the loader.

## The bootinfo block

Physical address `0x6000`, defined in `kernel/include/bootinfo.h`:

```c
struct e820_entry {          /* 24 bytes, packed */
    uint64_t base, len;
    uint32_t type;           /* 1 usable, 2 reserved, 3 ACPI reclaim, 4 NVS, 5 bad */
    uint32_t attr;           /* ACPI 3.0 extended attributes */
};

struct bootinfo {            /* packed */
    uint32_t magic;          /* 0x4E434C48 "HLCN" */
    uint16_t version;        /* 1 */
    uint8_t  boot_drive;     /* BIOS drive number */
    uint8_t  reserved0;
    uint32_t e820_count;     /* 1..64 */
    uint32_t reserved1;
    struct e820_entry e820[64];
};
```

The kernel panics if the magic, version, or entry count is invalid. Reserved fields are
zero in version 1; future versions may assign them.

## Low-memory map used during boot

| Physical range | Use |
|---|---|
| `0x6000` | bootinfo block |
| `0x7C00` | stage 1 (stack grows down from here) |
| `0x7E00` | stage 2 |
| `0x20000`–`0x27FFF` | 32 KiB disk read buffer |
| `0x70000`–`0x73FFF` | boot page tables (PML4, 2×PDPT, PD) |
| `0x100000` (1 MiB) | kernel load address (linked physical base) |
| `0x1000000` (16 MiB) | kernel ELF staging area |

Everything below 1 MiB plus the staging area is scratch: once the kernel owns memory
management it may reclaim all of it except the bootinfo block, which it must copy first.

<div style="page-break-after: always"></div>

# Memory Map

## Physical memory during boot

| Range | Owner | Use |
|---|---|---|
| `0x0000`–`0x04FF` | BIOS | IVT + BIOS data area (untouched) |
| `0x0500` / `0x100500` | stage 2 | A20 wraparound test bytes (saved/restored) |
| `0x6000`–`0x661F` | boot protocol | bootinfo block (16-byte header + 64×24-byte E820 entries) |
| `0x7C00`–`0x7DFF` | stage 1 | MBR code; stack grows down from `0x7C00` |
| `0x7E00`–… | stage 2 | loader code/data (≤ 127 sectors) |
| `0x20000`–`0x27FFF` | stage 2 | 32 KiB INT 13h read buffer |
| `0x70000`–`0x70FFF` | stage 2 | boot PML4 |
| `0x71000`–`0x71FFF` | stage 2 | boot PDPT (low, identity) |
| `0x72000`–`0x72FFF` | stage 2 | boot PDPT (high, kernel half) |
| `0x73000`–`0x73FFF` | stage 2 | boot PD (512 × 2 MiB = 1 GiB) |
| `0xB8000` | hardware | VGA text buffer |
| `0x100000` (1 MiB)–… | kernel | kernel image (`.text .rodata .data .bss`), linked here |
| `0x1000000` (16 MiB)–… | stage 2 | kernel ELF staging area |

After the kernel owns memory management (Phase 2), everything in this table except the
kernel image and hardware regions is reclaimable; the bootinfo block must be copied out
first.

## Virtual address space

Two regimes exist, before and after `vmm_init()`.

### During boot (stage 2's tables)

Stage 2 maps the first **1 GiB** of physical memory with 2 MiB pages, twice: identity at
`0` and at `KERNEL_VMA`. `hhdm_base` (see `memlayout.h`) starts at `KERNEL_VMA`, so
`phys_to_virt()` works for physical addresses below `BOOT_MAPPED_LIMIT` (1 GiB) only.
Early consumers (bootinfo validation, PMM construction, VMM table building) stay inside
that window.

### After `vmm_init()` (the kernel's tables)

| Virtual range | Maps to | Attributes |
|---|---|---|
| `0` .. `0x00007fffffffffff` | *unmapped* | userspace-to-be; null page faults |
| `HHDM_BASE = 0xffff800000000000` + `paddr` | all RAM + first 4 GiB (MMIO window) | 2 MiB global pages, NX; RAM write-back, non-RAM cache-disabled |
| `KERNEL_VMA = 0xffffffff80000000` + `paddr` | kernel image only | 4 KiB global pages, W^X (below) |

Kernel image protections (4 KiB granularity, boundaries page-aligned by the linker
script, verified by boot selftests):

```
_text_start   .. _text_end     RX   (executable, read-only)
_rodata_start .. _rodata_end   RO   (NX)
_data_start   .. _data_end     RW   (NX; covers .data and .bss)
```

`vmm_init()` also enables `EFER.NXE`, `CR0.WP`, and `CR4.PGE`, flips `hhdm_base` to
`HHDM_BASE` (the PMM re-derives its bitmap pointer at that moment via `pmm_rebase()`),
and installs a page-fault handler that decodes the error code and CR2 before panicking.
The boot identity map and the old 1 GiB alias are gone from that point; reserved E820
ranges above 4 GiB (e.g. the 64-bit PCI hole) are deliberately not mapped.

The frame allocator (`pmm`) is seeded from the E820 map minus the kernel image, low
memory, and its own bitmap; see `kernel/mm/pmm.c` for the construction order.

This document is updated in the same commit as any layout change.

<div style="page-break-after: always"></div>

# Threads and Scheduling

Phase 3 gives the kernel preemptive multitasking: kernel threads with their own
stacks, a round-robin scheduler driven by the timer tick, timed sleep, and
pthread-style join. Userspace processes (Phase 4) will build their kernel-side
execution on these threads.

## Layering

| Layer | File | Tested by |
|---|---|---|
| Policy (ready queue, sleep list) | `kernel/sched/sched_core.c` | host unit tests, ASan/UBSan |
| Mechanism (stacks, switch, timer, blocking) | `kernel/sched/sched.c` | in-kernel selftests + QEMU markers |
| Context switch | `kernel/arch/x86_64/ctx.asm` | in-kernel selftests |

`sched_core.c` is pure list manipulation over `struct thread` and never touches
hardware, locks, or allocation, so the identical code runs under sanitizers on
the host (`tests/host/test_sched.c`), including a 20,000-round randomized
stress run checked against a shadow model.

## Thread model

- Every thread is a kernel thread with a 16 KiB `kmalloc`'d stack (the boot
  context is adopted as thread 0, "main", on the boot stack; guard pages come
  with per-process address spaces in Phase 4).
- States: `READY` (on the ready queue), `RUNNING` (at most one), `SLEEPING`
  (on the wake-tick-sorted sleep list), `BLOCKED` (waiting in `thread_join`
  or parked via `sched_block()`/`sched_wake()` — the primitive `wait4` blocks
  on), `ZOMBIE` (exited, awaiting join).
- Threads are joinable, pthread-style: each created thread must be passed to
  `thread_join()` exactly once; join blocks until the thread exits, then frees
  its stack and control block. The selftests assert the heap returns to its
  pre-test object count, so leaks in this path cannot land.
- Returning from the entry function is equivalent to calling `thread_exit()`.

## Context switch

A thread off the CPU is exactly its saved `rsp`. `ctx_switch(&old->rsp,
new->rsp)` pushes the System V callee-saved registers (`rbp rbx r12-r15`) onto
the outgoing stack, swaps stack pointers, pops the incoming thread's registers,
and `ret`s. Caller-saved registers need no treatment: `ctx_switch` is an
ordinary function call, so the compiler already saved what mattered.

New threads get a hand-built frame whose return address is
`thread_entry_trampoline` with `r12` = entry function and `r13` = argument.
The trampoline is placed 8 bytes below the 16-byte-aligned stack top so `rsp`
is 16-aligned at its call sites, as the ABI requires. It enables interrupts,
calls `entry(arg)`, and falls into `thread_exit()`.

## Preemption

The 100 Hz PIT tick calls the scheduler's tick hook (registered via
`timer_set_tick_hook`), which wakes due sleepers and flags `need_resched`
whenever any thread is ready — the timeslice is one tick, 10 ms. The IRQ
dispatcher performs the actual switch *after* sending EOI (`sched_preempt()`),
so the in-service interrupt never blocks further ticks. The preempted thread's
trapframe simply stays parked on its own kernel stack; when scheduled back in,
it returns up through the interrupt path and `iretq` as if nothing happened.

Wakeups from interrupt handlers get the same treatment: making a thread ready
in interrupt context flags a reschedule that happens at IRQ exit, so a woken
thread runs within the same tick, not after the current timeslice expires.

## Invariants

1. `schedule()` is entered with interrupts disabled — always (asserted).
2. The running thread is never on the ready queue or the sleep list.
3. The idle thread never blocks, sleeps, exits, or enters the ready queue; it
   is the fallback when everyone else is blocked, and it `hlt`s.
4. A thread is on at most one list at a time (`next` is the only link).
5. A zombie's stack is freed only from `thread_join`, which can only run after
   the zombie's final `ctx_switch` has left that stack for good.

## Interrupt-off critical sections

One CPU, so disabling interrupts is the kernel lock. With preemption, any
state shared between threads needs it: the scheduler's own lists, the heap
(`kmalloc`/`kfree`), the frame allocator, and `kprintf`'s output emission
(formatting happens outside the critical section; only the console write is
atomic, keeping log lines whole). `cpu_irq_save()`/`cpu_irq_restore()` nest
correctly, so an allocator calling another allocator stays safe. These
sections become real spinlocks when SMP arrives.

## Proof of interleaving

Every boot runs a scheduler selftest whose result prints to serial and is
asserted by the QEMU integration test:

```
sched: online, round-robin, 10 ms timeslice
selftest: sched interleave "abcabcabcabc"
```

Three workers each log their tag and yield, four rounds; FIFO round-robin makes
the log a perfect rotation. The suite also proves timed sleep blocks for the
requested ticks, that a spinning thread which never yields cannot monopolize
the CPU (the sleeping main thread gets it back via preemption), and that join
reclaims every byte.

<div style="page-break-after: always"></div>

# Userspace and System Calls

Phase 4 brings ring 3; Phase 5 gives it files. This document covers user
address spaces, the SYSCALL/SYSRET path, the ELF64 loader, the process model —
fork, execve, wait4, exit over a host-tested process table — and the file
descriptor layer over the VFS (Appendix J).

## System call ABI

The native ABI is **identical to the Linux x86_64 syscall convention**, on
purpose: the Phase 7 Linux personality layer then shares one numbering, one
register convention, and one error vocabulary with native code.

| aspect | contract |
|---|---|
| entry | `syscall` instruction |
| number | `rax` |
| arguments | `rdi`, `rsi`, `rdx`, `r10`, `r8`, `r9` |
| return | `rax`; errors are `-errno` (see `kernel/include/errno.h`) |
| clobbered | `rcx` (return RIP), `r11` (RFLAGS) — hardware behavior |
| preserved | every other register — the entry stub saves and restores the full caller-saved set; callee-saved registers survive through the C ABI |
| unimplemented | `-ENOSYS`, always |

Implemented today (numbers from the Linux x86_64 table):

| # | syscall | scope |
|---|---|---|
| 0 | `read(fd, buf, count)` | any open fd; console reads block until input, then short-read |
| 1 | `write(fd, buf, count)` | any open fd; disk files are `-EBADF` until the 5d write path |
| 2 | `open(path, flags)` | access mode + `O_DIRECTORY` only; disk opens for write are `-EROFS` |
| 3 | `close(fd)` | drops the fd table's reference; last reference frees the description |
| 5 | `fstat(fd, statbuf)` | the Linux x86_64 144-byte `struct stat` (`_Static_assert`ed) |
| 8 | `lseek(fd, off, whence)` | SET/CUR/END on files and directories; `-ESPIPE` on the console |
| 39 | `getpid()` | the calling process's pid |
| 57 | `fork()` | full process clone (eager copy; COW later); child gets rax = 0 |
| 59 | `execve(path, argv, envp)` | replace the image with the ELF at `path` on the filesystem; SysV argv/envp stack; fds survive |
| 60 | `exit(status)` | zombie in the table, parent woken, every fd closed |
| 61 | `wait4(pid, wstatus, 0, NULL)` | blocking reap of one child (pid > 0 exact, -1 any); Linux `WIFEXITED` status encoding |
| 217 | `getdents64(fd, buf, count)` | real `linux_dirent64` records; directories synthesize `.` and `..` |

The fd-backed syscalls dispatch through the VFS `struct file_ops` vtable; a
`NULL` slot maps to the conventional errno (`write` on a read-only file
`-EBADF`, `lseek` on the console `-ESPIPE`, `getdents64` on a regular file
`-ENOTDIR`). The full open-file semantics live in Appendix J.

Every user pointer is validated before use: the range must lie below
`USER_VA_LIMIT` (`0x0000800000000000`, the canonical lower half) and every
page it touches must be present **and** user-accessible (`PTE_US`) in the
caller's address space. Anything else returns `-EFAULT` without being touched.

## The entry path

`syscall` does not switch stacks, so the entry stub
(`kernel/arch/x86_64/syscall_entry.asm`) runs its first instructions on the
*user* stack pointer with interrupts masked by SFMASK (`IF|TF|DF|AC`). It
parks the user RSP, adopts the current thread's kernel stack from a global the
scheduler maintains on every context switch (single CPU; `swapgs` + per-CPU
state arrive with SMP), saves every caller-saved user register (the ABI
promises they survive; the C dispatch would trash them), re-enables
interrupts — syscalls may block or be preempted — and calls
`syscall_dispatch()`. The return path disables interrupts, restores the
saved registers and the user RSP/RIP/RFLAGS, and `sysret`s. Init asserts
this contract at boot: it issues a syscall with sentinel values in all six
argument registers and verifies them afterwards.

MSR setup (`usermode.c`): `EFER.SCE`, `STAR` (selector layout in `gdt.h` was
designed for SYSRET's `+8/+16` selector math back in Phase 2), `LSTAR` →
entry stub, `SFMASK`.

The entry stub's register save area *is* `struct syscall_frame` (layout
asserted with `offsetof`): the complete user context, which is what makes
fork a struct copy. All kernel→ring 3 entries go through
`user_frame_enter(frame)` — an `iretq` with the frame's full register state.
A process's first entry is simply a zeroed frame with `rip`/`rsp`/`rflags`
set, so nothing of the kernel leaks into ring 3.

## Address spaces

`vmm_addrspace_create_user()` builds a PML4 whose upper half aliases the
kernel PML4's entries 256–511: the kernel is mapped (so interrupts, syscalls,
and the scheduler need no CR3 gymnastics) but inaccessible from ring 3 (no
kernel mapping carries `PTE_US`). Consequence, enforced by convention: the
kernel never adds *new top-level* PML4 entries after `vmm_init()` — all kernel
mappings live under the HHDM and kernel-image slots populated there.

The scheduler tracks an address space per thread (`NULL` = kernel). On a
context switch it reloads CR3 only when the target differs from what is
active, so pure kernel threads never pay for a TLB flush, and it always points
`TSS.rsp0` and the syscall stack global at the incoming thread's kernel stack.

User page permissions are real: code pages RX (no `PTE_W`), stack/data pages
RW + `PTE_NX`, and the page-table walk propagates `PTE_US` through the
intermediate levels while leaf entries decide the effective permission.

## Program loading: ELF64

Programs are statically linked ELF64 executables (`ET_EXEC`, `EM_X86_64`).
Loading is split the same way as every other core/kernel pair in the tree:

- **`elf64_validate()`** (`kernel/lib/elf64.c`) is a pure function over the
  image bytes, compiled for the host and tested under ASan/UBSan with a
  crafted well-formed executable plus one targeted mutation per rejection
  path. It checks the identity fields (magic, class, endianness, version,
  type, machine), the program header table bounds, and every `PT_LOAD`
  segment: file ranges in bounds (overflow-safely), `filesz ≤ memsz`, the
  vaddr range inside `[PAGE_SIZE, USER_VA_LIMIT)` (the null page is never
  mappable), vaddr/offset page congruence, no two segments sharing a page,
  no writable+executable segment, and an entry point inside an executable
  segment's file-backed bytes. The contract: after `ELF64_OK`, every
  arithmetic step the loader performs is overflow-free and in-bounds.
- **`elf64_load()`** (`kernel/proc/elf_load.c`) materializes a validated
  image: fresh zeroed frames per page (so `memsz > filesz` bss tails and
  segment padding arrive zeroed), file bytes copied in, and leaf permissions
  derived per segment — `PF_X` clears NX, `PF_W` sets writable, everything
  else is read-only NX. Rejection reasons surface as `elf64_strerror()` text.

`user/user.ld` links user programs at `0x400000` with three page-aligned
`PT_LOAD` segments (text R+X, rodata R, data+bss RW) so the per-segment W^X
policy is exercised by the very first binary. The ELF headers themselves are
not mapped into user memory; the kernel parses them from the kernel-side
buffer `vfs_read_file()` filled from disk.

## The process model

The process table itself is a pure state machine (`kernel/proc/proc_core.c`,
host-tested): up to 64 processes, monotonically increasing pids starting at
1 (init), parent links, `LIVE → ZOMBIE → free` lifecycle, and orphan
reparenting to init on parent exit. The kernel wraps it
(`kernel/proc/process.c`) with what a pure table cannot hold: the address
space, the hosting kernel thread, the blocked waiter, and the process name.

Each process also owns a **file descriptor table** (`FD_MAX` = 16 slots of
`struct file *`). An fd is an index into it; the object it names is a VFS open
file description, refcounted so `fork` can duplicate the table by pointer —
parent and child share each description, offset included, per POSIX. `execve`
leaves the table untouched (no close-on-exec flags yet — documented), and exit
closes everything. Init starts with fds 0/1/2 all referencing one open of
`/dev/console`.

Every process is **hosted by a kernel thread**. The thread builds nothing
itself — it receives a completed image (address space + start frame),
binds to the address space (`sched_set_addrspace`), and enters ring 3 via
`user_frame_enter`, never to return except through syscalls. This gives
processes preemption, sleep/wake, and join for free from the Phase 3
scheduler; the thread carries its process's pid.

The lifecycle syscalls:

- **`fork`** allocates a child pid, clones the address space eagerly
  (`paging_user_clone`: every lower-half 4 KiB mapping copied to a fresh
  frame, W/US/NX permissions preserved — COW comes later), copies the
  parent's saved syscall frame with `rax = 0`, and launches a hosting
  thread. The full-trapframe entry path is what makes this a struct copy.
- **`execve`** copies the path and both string vectors out of user memory
  first (bounded: 16 args, 128 bytes of strings, path ≤ 63), then builds the
  **complete new image before touching the old one** — address space, ELF
  segments, stack, argv/envp — so any failure (`-ENOENT`, `-ENOMEM`,
  `-E2BIG`) returns with the caller unharmed. On success it swaps the
  address space, reloads CR3, destroys the old space, and rewrites the saved
  syscall frame so `sysret` lands at the new entry point. The image comes off
  the filesystem: `vfs_read_file()` reads the whole ELF into a kernel buffer,
  the loader materializes it, the buffer is freed.
- **`exit`** marks the process a zombie holding its status, reparents its
  children to init, wakes the parent if it is blocked in `wait4`, and ends
  the hosting thread.
- **`wait4`** finds a matching child (exact pid or `-1` for any): a zombie
  is reaped — join the hosting thread, destroy the address space, free the
  table slot, deliver the Linux wait status (`(code & 0xff) << 8` for a
  normal exit, the signal number for a fault kill — `WIFEXITED` /
  `WIFSIGNALED` semantics); otherwise the parent publishes itself as the
  waiter and blocks. The check and the block happen in one interrupts-off
  section, so a child exiting concurrently cannot slip through unnoticed.

A hardware exception raised in ring 3 is never the kernel's problem: the
trap dispatcher logs one diagnostic line (exception, `rip`, error code,
`cr2` for page faults) and **kills the offending process** with the Linux
signal for that exception (`#PF`/`#GP` → `SIGSEGV`, `#UD` → `SIGILL`,
`#DE`/`#MF`/`#XM` → `SIGFPE`, ...), delivered to the parent through
`wait4`. The kernel and every other process keep running. Only
machine-level events (NMI, double fault, machine check) still panic, as
does any fault taken in kernel mode — that is a kernel bug by definition.
If init itself dies by signal, the kernel panics, Unix style.

New images start with the System V ABI stack contract: `[argc, argv...,
NULL, envp..., NULL, AT_NULL]` at a 16-byte-aligned `rsp`, string bytes
packed in the top stack page. `user/crt0.asm` picks `argc`/`argv` from it
and calls `main(argc, argv)`.

```
0x0000000000400000   .text    R+X
0x0000000000401000   .rodata  R
0x0000000000402000   .data + .bss  RW + NX
0x00007FFFFFFFB000   stack, 4 pages RW + NX
0x00007FFFFFFFF000   stack top; initial RSP just below, after argv/envp
```

## Init

Init is a freestanding C program: `user/crt0.asm` plus `user/init.c`, with
syscall wrappers in `user/syscall.h` and a tiny `user/ulib.h`. User code is
compiled without SSE and without the red zone — the kernel does not save
FPU/SSE state across context switches, and interrupts run on the current
stack. The linked ELFs are installed at `/bin` on the graphfs image
(`build/fs.img`, built by `tools/graphfs_mkfs.c`); boot reads `/bin/init`
through the VFS and joins it:

```
user: launching init (/bin/init from disk, 13448 bytes)
hello from ring 3
hello from execve
user: console open via /dev/console ok
user: C init: .data .bss .rodata ok
user: init exited (status 0)
```

Init doubles as the acceptance test for the loader, the ABI, the process
model, and the whole VFS read side. Its exit status names the first failed
check; 0 means all forty-seven passed: `write` returns the full length,
`.bss` zero-filled, `.data` initialized and writable, `getpid`,
`-ENOSYS`/`-EBADF`/`-EFAULT` error paths, all six argument registers
surviving a syscall, the full process round trip — `fork` returns a fresh
pid, the child `execve`s `/bin/hello` (which verifies its own argv arrived
intact and exits 42), `wait4` returns that pid with status `42 << 8`, a
second `wait4` returns `-ECHILD`, `execve` of an unknown path returns
`-ENOENT` — fault isolation: a forked child that writes to a kernel address
is killed with `SIGSEGV`, one that executes an illegal instruction with
`SIGILL`, both observed through `wait4` while everything else keeps running —
and the file surface: a known ELF opened and its magic read, `lseek` END/SET
proven against `fstat`'s size, `/bin` walked with `getdents64` and required
to hold exactly `.`, `..`, `init`, `hello`, `/dev/../bin/./hello` resolving
through the normalizer, and every error contract probed (`-ENOENT`,
`-ENOTDIR`, `-EISDIR`, `-EROFS`, `-EBADF` after close, `-ESPIPE` on the
console). After init is reaped, the kernel asserts the process table is
empty and that the physical frame count matches the pre-launch value: the
whole fork/exec/wait cycle leaks nothing.

## Known limits of this slice (by design, lifted in later slices)

- Static `ET_EXEC` only; `ET_DYN`/interpreters are Phase 7 territory.
- The root mount is read-only; `write` to disk files and the namespace
  syscalls (`mkdir`, `unlink`, ...) arrive with slice 5d.
- `FD_MAX` is 16, there is no `dup`/`dup2`, and no close-on-exec flags.
- No `chdir`: every process's working directory is `/`, and relative paths
  resolve from there (defined, not undefined).
- `fork` copies eagerly; no copy-on-write yet.
- `wait4` supports options 0 and a NULL rusage only; no `WNOHANG`, no
  process groups. No signal *delivery* exists yet — signal numbers appear
  only as fault-kill wait statuses.
- No SMEP/SMAP yet; the kernel relies on paging permissions plus pointer
  validation.
- The kernel does not save FPU/SSE state, so user code is built `-mno-sse`
  (enforced by `USER_CFLAGS`); lazy FPU switching comes later.

<div style="page-break-after: always"></div>

# Storage: PCI, virtio-blk, and the Block Layer

Phase 5 gives the kernel a disk. This document covers the PCI bus scan, the
VIRTIO 1.2 modern PCI transport, the virtio-blk driver, and the block layer
that filesystems build on. The native filesystem itself (graphfs) has its
own document: [graphfs.md](graphfs.md).

## Topology

The **boot disk** (kernel image) stays on the BIOS/INT13 path — the
bootloader owns it and the kernel never touches it again. The **filesystem
disk** (`build/fs.img`) is a second drive attached as a modern virtio-blk
PCI device:

```
qemu ... -drive file=fs.img,format=raw,if=none,id=fsdisk \
         -device virtio-blk-pci,drive=fsdisk,disable-legacy=on
```

`disable-legacy=on` makes QEMU expose the pure VIRTIO 1.x interface
(device id `1af4:1042`); the driver does not implement the pre-1.0 legacy
layout at all. virtio is not a shortcut: it is the industry-standard
paravirtual device interface, implemented here against the VIRTIO 1.2
specification. Bare-metal drivers (AHCI, NVMe) are additive later work
behind the same block API — a stated requirement, since the OS will
eventually be installed on real hardware.

## PCI (`kernel/drivers/pci.c`)

Configuration mechanism #1: the `0xCF8`/`0xCFC` port pair. `pci_init()`
performs a flat scan of every bus/device/function — the BIOS POST has
already assigned bus numbers and BARs, so everything answers at its final
address — logging each function and recording them in a fixed table:

```
pci: 00:01.1 8086:7010 class 01.01     ← PIIX3 IDE
pci: 00:04.0 1af4:1042 class 01.00     ← virtio-blk, modern
pci: 7 functions
```

The API (`pci.h`) offers config-space accessors, a bounded capability-list
walker, memory-BAR decoding (32- and 64-bit), and `pci_enable_device()`
(memory decoding + bus mastering). Port-based config access is inherently
x86; a future arch port swaps in ECAM behind the same header.

## virtio transport (`kernel/drivers/virtio_pci.c`)

The modern transport locates the device's register regions through
vendor-specific PCI capabilities (VIRTIO 1.2 §4.1.4): common config,
notify, and device config, each naming a BAR and offset. The regions are
reached through the direct map (BARs sit below 4 GiB, which `vmm_init()`
maps uncached); a BAR outside that window is rejected loudly.

Bring-up follows §3.1.1 exactly: reset (bounded wait), ACKNOWLEDGE, DRIVER,
feature negotiation — `VIRTIO_F_VERSION_1` is required, anything else is
per-driver — FEATURES_OK (verified by reading it back), queue setup,
DRIVER_OK. Any failure marks the device FAILED and leaves it quiescent.

**Virtqueue.** The split-ring bookkeeping — descriptor chains, free-list
recycling, available/used index math — is a pure module
(`kernel/drivers/virtq_core.c`) with no kernel dependencies, unit-tested on
the host under ASan/UBSan where the tests play the device's side of the
protocol (`tests/host/test_virtq.c`). The kernel transport supplies what a
pure module cannot: ring memory (two PMM frames per queue), physical
addresses, memory barriers, and the notify doorbell. The core also defends
against a misbehaving device: an out-of-range used id or a corrupted chain
cannot loop or overrun the driver.

v1 scope, stated: queue 0 only, no MSI-X, completions are polled. The
kernel is single-CPU and its callers block on I/O anyway, so interrupt
completion buys nothing yet; it arrives with async I/O.

## virtio-blk (`kernel/drivers/virtio_blk.c`)

Each request is the §5.2.6 three-descriptor chain: a 16-byte header
(type + starting sector, device-readable), one 4 KiB data buffer, and a
status byte (device-writable). The driver polls the used ring with a
2-second timer deadline — a dead device yields `-EIO`, never a hang — and
checks the device status byte before declaring success. Capacity is read
from device config via the generation counter, translating the device's
512-byte sectors to the kernel's 4 KiB blocks.

## Block layer (`kernel/block/block.c`)

Filesystems see an array of `BLOCK_SIZE` (4 KiB) blocks:

- `block_read`/`block_write` go through a 64-entry LRU cache
  (256 KiB of PMM frames). Writes are **write-through**: the cache never
  holds dirty data, so a crash can only lose what the filesystem had not
  yet ordered. `fsync`-driven flushing arrives with the write path (5d).
- Driver buffers must be physically contiguous; the cache's frame-backed
  entries satisfy this, and callers above the cache may pass any kernel
  memory.
- Concurrency contract: one caller at a time, asserted (`busy` guard).
  I/O takes milliseconds, so callers must not hold interrupts off. The
  contract is discharged by the VFS (Appendix J): every runtime disk path
  goes through `vfs.c`, which serializes behind one sleeping mutex;
  boot-time callers run before userspace exists.

Boot runs a self-test that round-trips a pattern through the *raw driver
ops* on the last block (a cached read-after-write would pass without
touching hardware), restores the original contents, then verifies the
cached path agrees:

```
virtio-blk: 16 MiB (32768 sectors), queue size 128
block: virtio-blk, 16 MiB (4096 blocks of 4096), cache 256 KiB
block: selftest passed (write/readback/restore)
```

A machine without a virtio-blk device still boots; storage-dependent
features report the absence explicitly.

## Known limits of this slice (lifted in later slices)

- Single block device; no partitions (the fs disk is one filesystem).
- Polled completion; no MSI-X, no async I/O, no request batching.
- Write-through cache only; no dirty tracking until fsync lands (5d).
- The block layer is single-caller by contract until the VFS adds a
  sleeping lock (5c).

<div style="page-break-after: always"></div>

# The VFS: One Namespace, Open Files, and devfs

Slice 5c gives processes files. This document is the contract for the virtual
filesystem layer: mounts and path resolution, the open-file abstraction and
per-process fd tables, the devfs device nodes, and the locking that lets every
process reach the disk. Chapter 14 of the book is the reasoning behind these
decisions; this is the reference. The layer below is graphfs (Appendix K) over
the block layer (Appendix I); the layer above is the syscall surface
(Appendix H).

## The objects

Three objects, defined in `kernel/include/vfs.h`:

- **`struct file_ops`** — a five-slot vtable: `read`, `write`, `lseek`,
  `fstat`, `getdents`. Every open object is fully described by one ops table
  plus a node id; nothing above the filesystems switches on file type. A
  `NULL` slot means "does not apply," and the syscall layer maps it to a fixed
  errno — the error vocabulary is part of the interface:

  | NULL slot | errno | rationale |
  |---|---|---|
  | `read` | `-EINVAL` | object is not readable |
  | `write` | `-EBADF` | Linux's answer for a read-only description |
  | `lseek` | `-ESPIPE` | the console is a stream, like a pipe |
  | `getdents` | `-ENOTDIR` | only directories enumerate |
  | `fstat` | — | mandatory; every ops table implements it |

- **`struct file`** — an *open file description* (the POSIX term): ops
  pointer, node id (graphfs node or devfs minor), byte **offset**, the `O_*`
  flags from open, and a reference count. The description — offset included —
  is shared by every fd that `fork` duplicated; it is freed when the last
  reference closes. Refcount updates run under `cpu_irq_save` (single CPU).

- **The mount table** — compile-time in v1: the graphfs on the registered
  block device is `/` (`st_dev` 1), devfs is `/dev` (`st_dev` 2). Resolution
  is a longest-prefix match on the canonical path; whatever no mount claims
  belongs to the root filesystem. `/dev` also exists on the graphfs image as
  an empty directory (`graphfs_mkfs --dir /dev`), a conventional mount point:
  the mount covers it, but `ls /` still lists it.

The **fd table** lives with the process (`struct process`, Appendix H):
`FD_MAX` = 16 pointers. `fork` copies the pointers and takes a reference each;
`execve` leaves the table alone (no close-on-exec flags yet); exit closes
everything. Init starts with fds 0/1/2 all referencing one open of
`/dev/console` — one description, three references.

## Path normalization

Every path entering the VFS is first normalized lexically by
`vfs_path_norm()` (`kernel/fs/vfs_path.c` — pure C, host-tested under
ASan/UBSan in `tests/host/test_vfs_path.c`):

- Duplicate slashes collapse; `.` and empty components disappear; a trailing
  slash is dropped; the output is always canonical and absolute.
- `..` pops the previous component; popping above the root stays at the root
  (POSIX: `/..` = `/`).
- Relative paths resolve from `/` — there is no `chdir` yet, so every
  process's working directory is *defined* to be `/`.
- Errors: `-EINVAL` for NULL/empty input, `-ENAMETOOLONG` for a component
  over `VFS_NAME_MAX` (255) or a result over `VFS_PATH_MAX - 1` (255) bytes.

Filesystems therefore never see `.` or `..`; `gfs_resolve()` stays a plain
component walk. **Lexical `..` is correct, not merely convenient, because the
graphfs v1 namespace is a strict tree with no symlinks** — every directory has
exactly one name, so dropping the last component of a canonical path *is* its
parent. If symlinks ever land, normalization must move into the resolution
walk; the comment at the top of `vfs_path.c` pins this dependency.

## open, and what each object supports

`vfs_open()` accepts the access mode plus `O_DIRECTORY`; any other flag
(`O_CREAT`, ...) is `-EINVAL` until the write path. The root mount is
read-only in 5c: opening a disk file `O_WRONLY`/`O_RDWR` is `-EROFS`, opening
any directory for write is `-EISDIR`, and `O_DIRECTORY` on a file is
`-ENOTDIR`.

| object | read | write | lseek | getdents | fstat mode |
|---|---|---|---|---|---|
| graphfs file | yes | — (`-EBADF`) | SET/CUR/END | — (`-ENOTDIR`) | `S_IFREG` |
| graphfs directory | `-EISDIR` | — | SET/CUR/END | yes | `S_IFDIR` |
| `/dev` directory | `-EISDIR` | — | SET/CUR | yes | `S_IFDIR \| 0755` |
| `/dev/console` | blocking | console | — (`-ESPIPE`) | — | `S_IFCHR \| 0666`, rdev 5:1 |

Offsets may seek past EOF (reads there return 0); a negative resulting offset
is `-EINVAL`. `fstat` fills the Linux x86_64 144-byte `struct stat`
(`kernel/include/stat.h`, `_Static_assert`ed): `st_ino` is the graphfs node id
or devfs minor, `st_blocks` counts 512-byte units, `st_blksize` is 4096.

`getdents64` emits real `linux_dirent64` records (8-byte aligned, `d_type`
filled). The file offset is a cursor, not a byte position: 0 is `.`, 1 is
`..`, then one position per outgoing edge. Non-NAME edges (TAG/REF — the
Phase 6 semantic layer) are not namespace entries and are skipped. A buffer
too small for even one record returns `-EINVAL` (Linux semantics); end of
directory returns 0. For `..` of the root, the root is its own parent, and
`/dev`'s `..` is the graphfs root — the two-namespace seam a caller never
sees.

Error mapping from the graphfs core is centralized (`gfs_errno`): `GFS_ENOENT
→ -ENOENT`, `GFS_ENOTDIR → -ENOTDIR`, `GFS_EISDIR → -EISDIR`,
`GFS_ENAMETOOLONG → -ENAMETOOLONG`, `GFS_EROFS → -EROFS`, `GFS_EINVAL →
-EINVAL`, and everything else — device failure, `GFS_EBADCRC`, detected
corruption — is `-EIO`: the data cannot be served, whatever the cause.

## devfs

`kernel/fs/devfs.c` is deliberately tiny: the `/dev` directory (three static
dirents: `.`, `..`, `console`) and `/dev/console`, the kernel console (serial
+ VGA) for output and the PS/2 keyboard for input. Node ids are devfs-local
(1 = directory, 2 = console) under `st_dev` 2, so they never collide with
graphfs inos. A path *through* a device (`/dev/console/x`) is `-ENOTDIR`, not
`-ENOENT`.

Console reads are terminal-style: **block until at least one character is
buffered, then return what is there** (a short read). The lost-wakeup race —
keyboard interrupt firing between "buffer empty" and "sleep" — is closed the
same way `wait4` closes it: the reader publishes itself and blocks inside one
interrupts-off section, and the keyboard IRQ handler wakes the published
reader through a one-line notify hook (`keyboard_set_notify`). A `read_lock`
mutex serializes concurrent readers so the single waiter slot suffices.

## Locking

The kernel's first sleeping lock (`kernel/sched/mutex.c`, `struct mutex`)
exists because disk I/O takes milliseconds with interrupts on, and after 5c
every process can reach the disk. Semantics:

- Contenders queue FIFO and block at scheduler level (no spinning); the
  waiter queue reuses `thread->next`, which a blocked thread is provably not
  using.
- `mutex_unlock` **hands the lock to the oldest waiter** before waking it —
  the lock is never observably free while anyone queues, so arrivals cannot
  barge past sleepers: FIFO fairness and starvation-freedom by construction.
- Thread context only (never an IRQ handler), non-recursive (relock by the
  owner asserts), unlock only by the owner. `mutex_held()` backs assertions.

One global `fs_lock` inside `vfs.c` serializes every graphfs core call and
every graphfs file-offset update — the `struct gfs` scratch buffers are
single-caller by contract. It is held for the duration of one operation,
never across a return to userspace. This lock also discharges the block
layer's documented "one caller at a time" rule for all runtime disk paths
(Appendix I); boot-time callers (`block_selftest`, the mount) run before
userspace exists. Coarse by design: one disk, polled I/O — a per-filesystem
lock buys nothing until there is concurrency to win back.

## exec from disk

`vfs_read_file(path, &buf, &size)` resolves a path to a DATA node and reads
the whole file into a fresh kmalloc buffer (empty files yield a 1-byte buffer
and size 0; a directory is `-EISDIR`). `process_execve()` and
`process_run_init()` load ELF images through it — the Phase 4 built-in program
table and the embedded `kernel/user_blob.asm` blob are deleted. The boot
marker proves it:

```
vfs: graphfs root mounted ro (gen 11, 4081/4096 blocks free, 1018/1024 nodes free)
vfs: devfs at /dev (console)
user: launching init (/bin/init from disk, 13448 bytes)
```

## Verification

- `vfs_path_norm` is host-tested exhaustively (canonical forms, `..` at every
  position, both `-ENAMETOOLONG` causes, `-EINVAL`).
- Init's acceptance suite (Appendix H) exercises the whole read surface from
  ring 3 — every syscall, every error contract, `getdents64` against the known
  image manifest, `/dev/../bin/./hello` through the normalizer — and the boot
  integration test asserts the markers and `status 0`.
- `graphfs_fsck` runs against `fs.img` in `make check`, so the image the
  kernel mounts is independently verified.

## Known limits of this slice (by design, lifted in later slices)

- The root mount is read-only; `write`, `mkdir`, `link`, `unlink`, `rename`,
  `fsync` and remount-writable are slice 5d.
- Mounts are compile-time; a `mount(2)` syscall is far future.
- No `chdir`/cwd, no `dup`/`dup2`, no `O_CLOEXEC`, `FD_MAX` = 16.
- One global filesystem lock; revisit when I/O stops being polled (or SMP).
- Lexical `..` normalization is valid only while the namespace is a strict
  tree with no symlinks (graphfs v1 policy).
- One console, one waiter slot; concurrent readers serialize.

<div style="page-break-after: always"></div>

# graphfs: The Native Property-Graph Filesystem

graphfs is the from-scratch native filesystem for this OS. It is not a clone
of ext2/FFS: on disk, everything is either a **node** (content + metadata) or
a **typed, named edge** between two nodes. The POSIX namespace is one edge
type layered on that graph — a "directory" is just a node whose outgoing
`NAME` edges are its entries — which leaves `TAG` and `REF` edges as a
first-class, format-stable substrate for the Phase 6 AI layer (provenance,
semantic links) with no on-disk change required.

The design is copy-on-write and self-checksumming, in the ZFS/APFS mould.
This document is **authoritative for the on-disk format (v1)**; the header
[`kernel/include/graphfs_core.h`](../kernel/include/graphfs_core.h) is the
authoritative API.

## Design principles

- **Copy-on-write, never overwrite.** A change writes fresh blocks and is
  made visible by a single atomic superblock write. A power loss either
  lands before that write (the change never happened) or after it (the change
  is whole). There is no journal and no repair-on-boot fsck — the on-disk
  image is *always* structurally consistent.
- **Self-validating tree.** Every metadata block is covered by a crc32c
  stored in the *pointer that reaches it*, not in the block itself (a
  `struct gfs_bp` = `{ phys, crc }`). The superblock, having no parent,
  checksums itself. Silent media corruption is detected on read, not served.
  v1 checksums all metadata; data-block checksums are a documented later
  extension (as btrfs shipped incrementally).
- **Two of everything live-critical.** Two superblock slots and two
  allocation-bitmap copies are selected by `generation & 1`, so a commit
  never writes the currently-live pair. Mount takes the valid superblock with
  the highest generation.
- **A pure core.** [`kernel/fs/graphfs_core.c`](../kernel/fs/graphfs_core.c)
  is plain C over an abstract block-device callback (`struct gfs_ops`), with
  no kernel dependencies and no dynamic allocation — block scratch lives in
  `struct gfs`; the writable allocator and fsck take caller-supplied buffers.
  The identical code compiles into the kernel, the host `mkfs`/`fsck` tools,
  and the ASan/UBSan host test suite. **One caller at a time per `struct
  gfs`** (a VFS sleeping lock will serialize this in 5c).

All integers are little-endian. Node `0` is the reserved null id; node `1`
(`GFS_ROOT_NODE`) is the root `NAMESPACE` node.

## Disk layout

Blocks are 4 KiB (`GFS_BLOCK_SIZE`). A freshly made filesystem lays out a
fixed prefix, then a copy-on-write region that grows on demand:

```
LBA 0            superblock slot 0        (holds even generations)
LBA 1            superblock slot 1        (holds odd generations)
LBA 2            ┐
   ...           ├ allocation bitmap, copy 0   (bitmap_blocks long)
LBA 2+B-1        ┘
LBA 2+B          ┐
   ...           ├ allocation bitmap, copy 1   (bitmap_blocks long)
LBA 2+2B-1       ┘
LBA 2+2B         node-map block           ┐ "data_first": the CoW region.
LBA 2+2B+1       root node-table block     │ mkfs seeds these two; every
   ...           edge blocks, node-table   │ later block is bitmap-allocated
   ...           blocks, file extents ...  ┘ on demand.
```

`B = bitmap_blocks = ceil(ceil(total_blocks/8) / 4096)`. The bitmap covers
*every* block including itself and the superblocks; the fixed-prefix blocks
are marked used at mkfs time. `data_first() = 2 + 2*bitmap_blocks` is the
first allocatable LBA, and the block allocator only ever hands out blocks
at or above it.

### Superblock (`SB_*` offsets, LBA 0 and 1)

Both slots share one format; the live one is `generation & 1`.

| Offset | Size | Field | Notes |
|-------:|-----:|-------|-------|
| 0  | 8 | magic          | `GFS_SB_MAGIC` (`"HRGRHFS1"`) |
| 8  | 4 | version        | `GFS_VERSION` = 1 |
| 12 | 4 | block_size     | 4096 |
| 16 | 4 | crc32c         | over the whole block with this field zeroed |
| 24 | 8 | generation     | monotonic; selects slot and bitmap copy |
| 32 | 8 | total_blocks   | device size in 4 KiB blocks |
| 40 | 8 | node_count     | table capacity, ≤ `GFS_MAX_NODES` (4096) |
| 48 | 8 | root_node      | always `1` |
| 56 | 8 | bitmap_start   | always `2` |
| 64 | 8 | bitmap_blocks  | length of one bitmap copy |
| 72 | 4 | bitmap_crc     | crc32c over the live bitmap copy |
| 80 | 8 | nodemap_phys   | LBA of the node-map block |
| 88 | 4 | nodemap_crc    | crc32c of the node-map block |
| 96 | 8 | free_blocks    | cached accounting |
| 104| 8 | free_nodes     | cached accounting |

Mount reads both slots, keeps the valid one with the highest generation,
then re-checks structural invariants beyond the checksum (`node_count ≥ 2`,
`root_node == 1`, `bitmap_start == 2`, node-map inside the data region, the
whole prefix fitting inside `total_blocks`). Any failure is `GFS_EBADFS`.

### Node map and node table

Nodes are 256-byte records, 16 per block (`GFS_NODES_PER_BLOCK`). The
**node-map** block is one 4 KiB block of 16-byte checksummed pointers
(`{ phys:8, crc:8-as-4 }`, `MP_SIZE` = 16), so at most 256 table blocks →
**4096 node ids** (`GFS_MAX_NODES`). Node `id` lives in table block
`map[id / 16]`, record `id % 16`.

Node record (`ND_*` offsets, 256 bytes):

| Offset | Size | Field | Notes |
|-------:|-----:|-------|-------|
| 0  | 4  | type       | `FREE` (0) / `NAMESPACE` (1) / `DATA` (2) |
| 4  | 4  | mode       | POSIX mode bits |
| 8  | 8  | size       | file length in bytes (DATA) |
| 16 | 4  | nlink      | incoming `NAME` edges |
| 20 | 4  | n_extents  | used inline extent runs |
| 24 | 8  | edge_count | outgoing edges, all types |
| 32 | 8  | parent     | `NAMESPACE`: its single parent node id |
| 40 | 8  | edge_phys  | first edge block (checksummed pointer…) |
| 48 | 4  | edge_crc   | …crc of that edge block |
| 56 | 8×24 | extents  | `GFS_INLINE_EXTENTS` = 8 runs |

An **extent** is `{ logical:8, phys:8, len:8 }` (`EXT_SIZE` = 24) — a run of
`len` contiguous blocks. v1 has no extent tree, so a file is at most 8 runs
(`GFS_EFRAG` past that); each run is up to 2³²−1 blocks, so the practical
cap is fragmentation, not size. The current writable path additionally caps a
single file at `GFS_MAX_FILE_BLOCKS` (512 blocks = 2 MiB).

### Edge blocks

Outgoing edges hang off a node in a singly-linked chain of edge blocks. Each
block is a 24-byte header + 14 fixed 272-byte records (`GFS_EDGES_PER_BLOCK`):

- Header (`EB_*`): `magic` (`"GDGE"`), `count`, and a checksummed
  `{ next_phys, next_crc }` pointer to the continuation block.
- Record (`ER_*`, 272 bytes): `type:4`, `namelen:4`, `target:8`, and a
  `name` of up to `GFS_NAME_MAX` = 255 bytes.

Edge types: `NAME` (1) builds the POSIX tree; `TAG` (2) and `REF` (3) are
valid on disk today but ignored by path resolution — reserved for the Phase 6
semantic layer.

## Namespace policy (v1)

This is a *link-time policy*, not a format limitation:

- A **`NAMESPACE`** node has exactly one incoming `NAME` edge — a single
  parent. So `..` is a single stored field, and directory cycles are
  impossible. A second `NAME` edge onto a namespace is `GFS_EMANYPARENTS`.
- A **`DATA`** node may have any number of incoming `NAME` edges: hard links.
  Unlinking to `nlink == 0` frees the node and its blocks.
- `gfs_unlink` on a non-empty namespace is `GFS_ENOTEMPTY`.

`.` and `..` are the VFS's concern; the core's `gfs_resolve` walks plain path
components only. Multi-parent namespaces are a documented later extension.

## The write transaction

Writable mounts carry two allocator bitmaps carved from the caller's work
buffer (`gfs_mount_work_size` = `2 * ceil(total_blocks/8)` bytes):

- **`committed_bm`** — the live on-disk allocation state.
- **`working_bm`** — the current transaction's view.

Allocation draws only from blocks free in **both** bitmaps, so a block freed
this transaction — still reachable through the one-generation fallback
superblock — is never reused until the transaction commits. Each mutating
call (`gfs_node_create`, `gfs_link`, `gfs_unlink`, `gfs_write`) is its own
transaction: it CoWs the metadata it touches up to the node map, writes the
inactive bitmap copy and inactive superblock slot at `generation + 1`, and
returns only once that superblock write lands. There is no partial write on
`GFS_ENOSPC`.

## Tools and testing

Because the core is pure, the host tools are thin drivers over the exact code
the kernel mounts — the format has a single implementation:

- [`tools/graphfs_mkfs.c`](../tools/graphfs_mkfs.c) — `graphfs_mkfs --out
  img --size-mib N [/path=host_file ...]` makes a filesystem and installs
  host files at absolute paths, creating intermediate namespaces. The build
  uses it to lay `/bin/init` and `/bin/hello` onto `build/fs.img`.
- [`tools/graphfs_fsck.c`](../tools/graphfs_fsck.c) — `graphfs_fsck img`
  runs `gfs_fsck` over the image. Since a healthy CoW+checksummed image
  always passes, a failure means a core bug or media corruption. `make
  check-fsck` gates the freshly built image; the 5d crash-consistency gate
  will run the same check after boot.

Three test levels cover the format:

- **Host unit tests** — [`tests/host/test_graphfs.c`](../tests/host/test_graphfs.c),
  built into `make check-host` under ASan/UBSan against an in-memory device.
- **fsck gate** — `make check-fsck`, above.
- **Boot integration** — the kernel mounts `build/fs.img` over virtio-blk
  during `make check-boot` (read path in 5c).

## v1 limits (carried, by design)

- Metadata-only checksums (no data-block checksums yet).
- 8 inline extents per file, 512-block (2 MiB) max file, 4096 nodes.
- Single-parent namespaces; one caller at a time per handle.
- No extent tree, no snapshots surfaced, no compression.

Each is a documented future extension, not a shortcut: the CoW +
checksummed-pointer format was chosen so these can be added without breaking
the on-disk layout.

<div style="page-break-after: always"></div>

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

It asserts that the expected markers appear on the serial console **in order** —
the authoritative list is `PASS_MARKERS` in `tests/run_qemu.py`, one marker per
proven subsystem, from the banner through memory, scheduling, the in-kernel
self-tests, and the ring 3 round trip (`hello from ring 3` is printed by user
code) to `boot: complete`.

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

<div style="page-break-after: always"></div>
