# Memory Map

## Physical memory during boot

| Range | Owner | Use |
|---|---|---|
| `0x0000`–`0x04FF` | BIOS | IVT + BIOS data area (untouched) |
| `0x0500` / `0x100500` | stage 2 | A20 wraparound test bytes (saved/restored) |
| `0x6000`–`0x661F` | boot protocol | bootinfo block (16-byte header + 64×24-byte E820 entries) |
| `0x7C00`–`0x7DFF` | stage 1 | MBR code; stack grows down from `0x7C00` |
| `0x7E00`–… | stage 2 | loader code/data (≤ 127 sectors) |
| `0x20000`–`0x27FFF` | stage 2 | 32 KiB INT 13h read buffer |
| `0x70000`–`0x70FFF` | stage 2 | boot PML4 |
| `0x71000`–`0x71FFF` | stage 2 | boot PDPT (low, identity) |
| `0x72000`–`0x72FFF` | stage 2 | boot PDPT (high, kernel half) |
| `0x73000`–`0x73FFF` | stage 2 | boot PD (512 × 2 MiB = 1 GiB) |
| `0xB8000` | hardware | VGA text buffer |
| `0x100000` (1 MiB)–… | kernel | kernel image (`.text .rodata .data .bss`), linked here |
| `0x1000000` (16 MiB)–… | stage 2 | kernel ELF staging area |

After the kernel owns memory management (Phase 2), everything in this table except the
kernel image and hardware regions is reclaimable; the bootinfo block must be copied out
first.

## Virtual address space

Two regimes exist, before and after `vmm_init()`.

### During boot (stage 2's tables)

Stage 2 maps the first **1 GiB** of physical memory with 2 MiB pages, twice: identity at
`0` and at `KERNEL_VMA`. `hhdm_base` (see `memlayout.h`) starts at `KERNEL_VMA`, so
`phys_to_virt()` works for physical addresses below `BOOT_MAPPED_LIMIT` (1 GiB) only.
Early consumers (bootinfo validation, PMM construction, VMM table building) stay inside
that window.

### After `vmm_init()` (the kernel's tables)

| Virtual range | Maps to | Attributes |
|---|---|---|
| `0` .. `0x00007fffffffffff` | *unmapped* | userspace-to-be; null page faults |
| `HHDM_BASE = 0xffff800000000000` + `paddr` | all RAM + first 4 GiB (MMIO window) | 2 MiB global pages, NX; RAM write-back, non-RAM cache-disabled |
| `KERNEL_VMA = 0xffffffff80000000` + `paddr` | kernel image only | 4 KiB global pages, W^X (below) |

Kernel image protections (4 KiB granularity, boundaries page-aligned by the linker
script, verified by boot selftests):

```
_text_start   .. _text_end     RX   (executable, read-only)
_rodata_start .. _rodata_end   RO   (NX)
_data_start   .. _data_end     RW   (NX; covers .data and .bss)
```

`vmm_init()` also enables `EFER.NXE`, `CR0.WP`, and `CR4.PGE`, flips `hhdm_base` to
`HHDM_BASE` (the PMM re-derives its bitmap pointer at that moment via `pmm_rebase()`),
and installs a page-fault handler that decodes the error code and CR2 before panicking.
The boot identity map and the old 1 GiB alias are gone from that point; reserved E820
ranges above 4 GiB (e.g. the 64-bit PCI hole) are deliberately not mapped.

The frame allocator (`pmm`) is seeded from the E820 map minus the kernel image, low
memory, and its own bitmap; see `kernel/mm/pmm.c` for the construction order.

This document is updated in the same commit as any layout change.
