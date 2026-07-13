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

Rule 2 of the testing policy (Chapter 14): a bug fix lands with a test that
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
  commit added a "preserved" row to `docs/userspace.md`'s ABI table. The next
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
the regions that cannot yet speak (Chapter 14 §14.3). Second, **the last
move of every hunt is a test** — the table gets you to a cause, but the hunt
ends only when the cause cannot recur unannounced. That is the difference
between "fixed it" and "finished it," and it is the through-line of all
three stories above.
