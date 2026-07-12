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
