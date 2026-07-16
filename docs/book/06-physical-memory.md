# Chapter 6 — Physical Memory

Every allocation the kernel will ever make — page tables, heap slabs, thread
stacks, process pages, filesystem buffers — ultimately comes from *physical
RAM*, measured in 4 KiB frames. Before any of that, something has to own the
question "which physical frames are free?" That something is the physical memory
manager (PMM), and it is the first place the core/driver split earns its keep on
memory itself.

## 6.1 From a BIOS memory map to a claim on RAM

The PMM's input is the E820 map the bootloader collected — a list of physical
ranges tagged usable, reserved, ACPI-reclaimable, ACPI-NVS, or bad. Only the
*usable* ranges are RAM the kernel may touch, and even those are not entirely
free: some of that RAM currently holds the kernel image, the bootinfo block, and
low-memory boot scratch. So `pmm_init()` (`kernel/mm/pmm.c`) must take the raw
E820 map and subtract everything already spoken for.

The construction order is where correctness lives, and it is worth stating
precisely (`kernel/mm/pmm.c`):

1. Scan the usable E820 entries to find the highest physical address, which sizes
   the allocator.
2. Represent free/used as a **bitmap** — one bit per 4 KiB frame. A machine with
   256 MiB has 65,536 frames, so the bitmap is 8 KiB: cheap, and O(1) to test or
   set a specific frame.
3. Place that bitmap somewhere in usable memory — a chicken-and-egg step, since
   the allocator needs memory to describe memory.
4. Mark **everything** used, then walk the usable E820 ranges and mark only those
   frames free.
5. Re-mark as used the ranges that are usable RAM but not available: the kernel
   image (from the linker symbols), low memory, and the bitmap's own frames.

Step 4's "mark everything used first, then free the usable ranges" is the safe
default in disguise. If you did it the other way — mark everything free, then
carve out the reserved parts — a range you forgot to carve out becomes a frame
you hand to the heap that is actually MMIO or ACPI tables, and the corruption is
silent. Starting from "all used" means a bug makes you run out of memory (loud,
early, obvious) rather than hand out memory you do not own (silent, late,
catastrophic). **When the two failure modes are "too conservative" and "silent
corruption," always arrange for your bugs to land on the conservative side.**

## 6.2 The pure core, again

The allocator's logic — find a clear bit, set it, return the frame; clear a bit
to free — is pure bitmap arithmetic with no hardware in it. So it lives in
`kernel/mm/pmm_core.c` and is tested under ASan/UBSan in `tests/host/test_pmm.c`,
while `pmm.c` handles the parts that *are* environmental: reading E820, the
interrupts-off locking, and the awkward bootstrap of where the bitmap physically
sits.

This split pays off precisely because bitmap code is a minefield of off-by-ones —
the frame that is bit `i` of byte `i/8`, the boundary where a range does not end
on a byte edge, the "find first free" scan that must not run off the end. Those
are exactly the bugs sanitizers and exhaustive host tests catch and a
freestanding kernel cannot. The rule from Chapter 1 in concrete form: the index
math is pure and tested on your Mac; only the hardware wiring is trusted to the
kernel build.

## 6.3 The rebasing trick: an allocator that outlives its own addressing

There is a genuinely tricky lifecycle problem here that illustrates how careful
kernel bring-up has to be about *its own* addresses. The PMM is built during boot
while the only virtual mapping in effect is the bootloader's temporary 1 GiB
window (identity plus the higher-half kernel alias). The PMM's bitmap pointer is
therefore a virtual address that is only valid *under those temporary tables*.

Then `vmm_init()` (next chapter) builds the kernel's real page tables and, in
particular, moves the "direct map" of all physical RAM from the boot window's
base to the kernel's permanent HHDM base (`0xffff800000000000`). At that instant,
the virtual address the PMM was using for its bitmap **ceases to be mapped**. If
nothing accounted for this, the first post-`vmm_init` allocation would fault.

The solution is `pmm_rebase()`: at the exact moment `vmm_init()` flips the
direct-map base, it tells the PMM to re-derive its bitmap pointer from the new
base. The physical location of the bitmap never moved; only the virtual address
you reach it through changed, and the PMM recomputes that address. This is a
recurring hazard in kernels — a data structure whose *physical* home is stable
but whose *virtual* address depends on which page tables are live — and the
discipline is to make the dependency explicit and update it at the one moment the
mapping changes, rather than hope the old address stays valid. The memory-map doc
calls this out at the seam; the code performs it at the seam. Interfaces this
sharp are only safe when the sharpness is documented at the exact point it
matters.

## 6.4 What "reserved above 4 GiB" teaches about honesty

One more decision from Appendix F: reserved E820 ranges above 4 GiB —
for instance the 64-bit PCI MMIO hole — are *deliberately not mapped* by the
kernel. The kernel maps all RAM plus the first 4 GiB (to reach legacy MMIO), and
stops. Anything the hardware placed above 4 GiB that the kernel does not yet
drive is simply absent from the address space.

That is complete-or-absent applied to the address space itself. Rather than
mapping "everything, just in case" — which would mean the kernel could touch
device memory it has no driver for and no understanding of — the map contains
exactly what the kernel can currently account for. When a driver for something up
there arrives, the mapping arrives with it. The address space is a claim about
what the kernel understands, and it should never claim more than it does.

## 6.5 The transferable lessons

- **Arrange for bugs to fail conservatively.** "Mark all used, then free what is
  usable" turns a forgotten reserved range into out-of-memory instead of silent
  corruption. Choose the initialization order that makes your worst bug loud.
- **The index math is pure — test it with a safety net.** Bitmap allocators are
  off-by-one factories; the core is host-tested under sanitizers, and only the
  E820 wiring and locking is trusted to the kernel.
- **A physical structure can outlive its virtual address.** When you change
  which page tables are live, every pointer whose validity depended on the old
  tables must be re-derived at that instant. Make the dependency explicit at the
  seam.
- **Map only what you understand.** The address space should reflect what the
  kernel can account for, not everything the hardware exposes.

The PMM hands out anonymous 4 KiB frames of physical RAM. It has no idea what a
"process" or a "read-only page" is — it just tracks free versus used. Turning
those flat frames into a structured, protected virtual address space, with the
kernel safely up in the higher half and its own code un-writable, is the job of
the virtual memory manager. That is the next chapter, and it is the heart of the
kernel.
