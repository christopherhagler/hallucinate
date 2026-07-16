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
