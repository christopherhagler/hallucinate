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
