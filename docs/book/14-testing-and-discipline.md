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
