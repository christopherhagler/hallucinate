# Boot Protocol (version 1)

This document is the contract between the Hallucinate OS bootloader and the kernel. It is
versioned; any incompatible change bumps `BOOTINFO_VERSION` in `kernel/include/bootinfo.h`
and this document together.

## Disk layout

The system boots from a raw disk image assembled by `tools/mkimage.py`:

| LBA | Contents |
|---|---|
| 0 | Stage 1 (MBR, exactly 512 bytes, `0xAA55` signature) |
| 1 .. N | Stage 2, sector-padded (N ≤ 127 so stage 1 can load it in one INT 13h read) |
| N+1 .. | Kernel ELF64 image, sector-padded |

The image is zero-padded to a whole MiB.

### Build-time patching

Neither stage hardcodes the image geometry. `mkimage.py` locates unique markers in the
assembled binaries and patches them:

- **Stage 1**: marker `"HB1\0"` immediately precedes its Disk Address Packet. The DAP
  sector-count word (marker offset +6) receives the stage-2 sector count. `mkimage`
  verifies the DAP header bytes (`0x10 0x00`) follow the marker.
- **Stage 2**: marker `"HB2\0"`; offset +4 holds the kernel start LBA (u64), offset +12
  the kernel sector count (u32).

## Stage 1 (`boot/stage1.asm`)

Loaded by the BIOS at `0x7C00` with the boot drive number in `DL`.

1. Canonicalizes `CS:IP` via a far jump, zeroes segment registers, sets the stack just
   below `0x7C00`.
2. Verifies INT 13h extensions (`AH=41h`, signature `0x55AA`); LBA reads are required.
3. Reads stage 2 to `0x7E00` with INT 13h `AH=42h`, retrying up to 3 times with a disk
   reset between attempts.
4. Verifies the first dword of stage 2 is `0x32534C48` (`"HLS2"`).
5. Far-jumps to `0:0x7E04` with `DL` still holding the boot drive.

On any failure it prints an `ERR:`-prefixed message and halts. The integration test
harness treats `ERR:` on the serial console as fatal.

## Stage 2 (`boot/stage2.asm`)

Entered in 16-bit real mode at `0x7E04`.

1. **A20**: tests for wraparound at `0x100500`; if disabled, tries INT 15h `AX=2401h`,
   then port `0x92` (fast A20), then the 8042 keyboard controller. All KBC waits are
   bounded (64K polls). Failure is fatal.
2. **E820**: walks INT 15h `AX=E820h`, storing up to 64 raw 24-byte entries directly into
   the bootinfo block. The ACPI extended-attributes dword is preset to 1 before each call
   so 20-byte BIOS replies remain valid. At least one entry is required.
3. **Kernel load**: reads the kernel image in 64-sector (32 KiB) chunks to a real-mode
   buffer at `0x20000`, then copies each chunk to the staging area at `0x1000000` (16 MiB)
   using unreal mode (`a32 rep movsd` with interrupts disabled). Unreal mode is re-entered
   after every INT 13h call because the BIOS may reset cached descriptor limits. Reads
   retry 3 times.
4. **Protected mode**: loads a GDT (null, 32-bit code `0x08`, data `0x10`, 64-bit code
   `0x18`) and enters 32-bit protected mode.
5. **Paging**: builds 4-level page tables at `0x70000`–`0x73FFF` mapping the first 1 GiB
   of physical memory twice with 2 MiB pages: identity (`PML4[0]`) and at the kernel VMA
   (`PML4[511] → PDPT[510]`, i.e. `0xffffffff80000000`). Both mappings share one page
   directory.
6. **Long mode**: sets CR4.PAE, loads CR3, sets EFER.LME (MSR `0xC0000080` bit 8), enables
   paging, and far-jumps to 64-bit code.
7. **ELF load** (64-bit): validates the staged image (ELF magic, 64-bit, little-endian,
   `EM_X86_64`) and for each `PT_LOAD` segment checks `p_filesz ≤ p_memsz`,
   `p_paddr ≥ 0x100000`, and that the segment ends below the staging area; then copies
   file bytes to `p_paddr` and zeroes BSS. The entry point must be ≥
   `0xFFFFFF8000000000`.
8. Jumps to the ELF entry point with `RDI` = physical address of the bootinfo block.

Errors in 64-bit mode (no BIOS available) are reported white-on-red via the VGA text
buffer at `0xB8000`.

## Kernel entry state

At `_start` (`kernel/arch/x86_64/entry.asm`):

- CPU in 64-bit long mode, interrupts disabled, paging per step 5 above.
- `RDI` = physical address of the bootinfo block (`0x6000`).
- GDT is stage 2's; the kernel must install its own before relying on selectors.
- No stack guarantee: the kernel entry stub installs its own 16 KiB stack.
- All kernel `PT_LOAD` segments copied and BSS zeroed by the loader.

## The bootinfo block

Physical address `0x6000`, defined in `kernel/include/bootinfo.h`:

```c
struct e820_entry {          /* 24 bytes, packed */
    uint64_t base, len;
    uint32_t type;           /* 1 usable, 2 reserved, 3 ACPI reclaim, 4 NVS, 5 bad */
    uint32_t attr;           /* ACPI 3.0 extended attributes */
};

struct bootinfo {            /* packed */
    uint32_t magic;          /* 0x4E434C48 "HLCN" */
    uint16_t version;        /* 1 */
    uint8_t  boot_drive;     /* BIOS drive number */
    uint8_t  reserved0;
    uint32_t e820_count;     /* 1..64 */
    uint32_t reserved1;
    struct e820_entry e820[64];
};
```

The kernel panics if the magic, version, or entry count is invalid. Reserved fields are
zero in version 1; future versions may assign them.

## Low-memory map used during boot

| Physical range | Use |
|---|---|
| `0x6000` | bootinfo block |
| `0x7C00` | stage 1 (stack grows down from here) |
| `0x7E00` | stage 2 |
| `0x20000`–`0x27FFF` | 32 KiB disk read buffer |
| `0x70000`–`0x73FFF` | boot page tables (PML4, 2×PDPT, PD) |
| `0x100000` (1 MiB) | kernel load address (linked physical base) |
| `0x1000000` (16 MiB) | kernel ELF staging area |

Everything below 1 MiB plus the staging area is scratch: once the kernel owns memory
management it may reclaim all of it except the bootinfo block, which it must copy first.
