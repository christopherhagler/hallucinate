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
| 14 | [Testing and Professional Discipline](14-testing-and-discipline.md) | The three-level test strategy, and how to make systems code testable |
| 15 | [Where to Go Next](15-where-to-go-next.md) | The road from here to a self-hosting, AI-native OS |
| A | [The Folklore Margin](appendix-a-folklore.md) | The tacit *whys* behind the code's decisions — naive alternative, real reason, failure avoided |
| B | [The Lab Book](appendix-b-lab-book.md) | Graded hands-on labs: diagnosis drills, reproduce-from-tests, extensions, comparative reading |

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

This codebase has a rule, stated in `docs/architecture.md` as a design
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
failure. `docs/userspace.md` has a whole "Known limits of this slice" section:
eager fork with no copy-on-write, no `WNOHANG`, no FPU save, static `ET_EXEC`
only. Each is a deliberate, bounded scope with an explicit failure for anything
outside it. That is what "professional" means in kernel work — not that
everything is done, but that the boundary between done and not-done is exact and
enforced by the code.

## 1.4 State your invariants in one sentence

Concurrency and hardware make kernel state fragile. The defense is to keep, for
each subsystem, a short list of invariants so simple you can check them by
inspection. The scheduler's list (`docs/scheduling.md`) is the model:

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
automated checking accumulate for the life of the codebase (Chapter 14 is the
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
`docs/boot-protocol.md`; here is what each move *is* and why it is hard.

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
**versioned interface** (`docs/boot-protocol.md`, `BOOTINFO_VERSION`). The
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

## 4.3 The GDT and TSS: segmentation's vestige, and the double-fault stack

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

## 4.4 The IDT: 256 vectors and one dispatcher

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

## 4.5 The transferable lessons

- **Bring up your output before anything that might fail.** You cannot debug
  what you cannot see, and the first subsystem to break is often an early one.
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

One more decision from `docs/memory-map.md`: reserved E820 ranges above 4 GiB —
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
kernel's permanent address space (`docs/memory-map.md`):

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
fact. `docs/memory-map.md`: protections are "verified by boot selftests." This is
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
(`docs/userspace.md`: "the whole fork/exec/wait cycle leaks nothing"). A single
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

The states a thread moves through (`docs/scheduling.md`):

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
`docs/graphfs.md`; this chapter is the reasoning behind it.

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

The on-disk geometry (full table in `docs/graphfs.md`, offsets in
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

# Chapter 14 — Testing and Professional Discipline

Every subsystem chapter ended with the machine proving something on every boot.
This chapter steps back and treats that as the subject in its own right, because
the testing strategy is not a supporting character in this project — it is the
main reason the project can exist at all. An OS is too large to hold in your head
and too unforgiving to debug by running it once. The thing that lets you build it
anyway is a test architecture that makes correctness *cumulative*: every property
you establish stays established forever. If you internalize one chapter's *method*
rather than its facts, make it this one.

## 14.1 Three levels, three kinds of truth

`make check` is the gate for every commit, and it runs three levels of testing
that catch three genuinely different classes of bug (`docs/testing.md`).

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

## 14.2 Markers as an append-only ledger of proven behavior

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

## 14.3 Design your failures to be loud and machine-detectable

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

## 14.4 The policy that ties it together

Three rules govern the whole project (`docs/testing.md`), and they are worth
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

## 14.5 Why this is the chapter that matters most

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

## 14.6 The transferable lessons

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

<div style="page-break-after: always"></div>

# Chapter 15 — Where to Go Next

You have followed the system from the firmware's first instruction to a
crash-consistent filesystem holding the programs the kernel boots. That is a
complete vertical slice of an operating system — bootloader, memory, scheduling,
userspace, storage, filesystem — and understanding it end to end puts you well
past where most people who "know OS concepts" actually are, because you have seen
how the concepts *connect* rather than as isolated exam answers. This final
chapter is about turning that understanding into mastery: what to build next, and
how to keep growing.

## 15.1 The road this project is on

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

## 15.2 How to actually get better at this

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

## 15.3 The habits that compound

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

## 15.4 A closing word

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
machine demonstrates it (Chapter 14), including for your homework.

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
paragraph — the full Chapter 14 definition of done.

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

