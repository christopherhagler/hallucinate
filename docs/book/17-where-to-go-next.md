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

- **Round out the filesystem.** Phase 5 is complete — VFS, devfs,
  exec-from-disk, and the full write path with its fsck-after-boot gate — but
  the deferred conveniences are exactly the right size for solo practice:
  `chdir` and real relative paths, `dup`/`dup2` and `O_CLOEXEC`, `ftruncate`,
  a `mount(2)` syscall over the compile-time table, POSIX unlink-while-open
  semantics (on-disk orphan tracking — study how ext4's orphan list survives
  a crash first). Each one touches an interface the earlier chapters pinned,
  so you will feel exactly what the pinning bought.
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
