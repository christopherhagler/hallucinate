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
