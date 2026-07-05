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

The kernel is a **higher-half kernel** linked at:

```
KERNEL_VMA = 0xffffffff80000000   (kernel/include/memlayout.h)
```

Boot page tables (built by stage 2) map the first **1 GiB** of physical memory with
2 MiB pages, twice:

| Virtual range | Maps to | Purpose |
|---|---|---|
| `0x0000000000000000` + 1 GiB | phys `0`–1 GiB | identity map (bootstrap only; dropped in Phase 2) |
| `0xffffffff80000000` + 1 GiB | phys `0`–1 GiB | kernel half; all kernel addresses |

Both mappings share a single page directory, so they are identical by construction.
`BOOT_MAPPED_LIMIT` (1 GiB) bounds what `phys_to_virt`/`virt_to_phys` may touch until the
real VMM exists; the kernel panics on any bootinfo pointer outside it.

Kernel virtual layout within the higher half:

```
0xffffffff80000000   KERNEL_VMA (phys 0 alias)
0xffffffff80100000   kernel image start (_kernel_start, phys 0x100000)
    .text  .rodata  .data  .bss     (each 4 KiB-aligned, see kernel/linker.ld)
                     kernel image end (_kernel_end)
0xffffffff800b8000   VGA text buffer alias used by the VGA driver
```

## Planned evolution (Phase 2)

- The kernel installs its own page tables: identity map dropped, kernel half retained,
  a physical-map window (`HHDM`-style) added for frame access, 4 KiB granularity where
  protection demands it (`.rodata` read-only, `.text` execute-only data-wise, NX
  elsewhere).
- The physical frame allocator is seeded from the E820 map minus the kernel image and
  bootinfo-derived reservations.
- This document is updated in the same commit as any layout change.
