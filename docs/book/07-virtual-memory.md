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
kernel's permanent address space (Appendix F):

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
fact. Appendix F: protections are "verified by boot selftests." This is
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
