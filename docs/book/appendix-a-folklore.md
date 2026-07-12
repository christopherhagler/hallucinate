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
