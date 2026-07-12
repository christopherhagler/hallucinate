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
