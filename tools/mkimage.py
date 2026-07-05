#!/usr/bin/env python3
"""mkimage - assemble and validate the Hallucinate OS disk image.

Disk layout (see docs/boot-protocol.md):

    LBA 0                stage 1 (MBR, 512 bytes)
    LBA 1..N             stage 2, sector-padded (N <= 127)
    LBA N+1..            kernel ELF image, sector-padded

Build-time patching:
    stage 1: marker b"HB1\\0" + Disk Address Packet; the DAP sector-count
             word (marker offset +6) receives the stage-2 sector count.
    stage 2: marker b"HB2\\0"; +4 u64 kernel LBA, +12 u32 kernel sectors.

The kernel ELF is validated against the boot protocol before it is accepted:
64-bit little-endian x86_64 executable, all PT_LOAD segments inside
[1 MiB, 16 MiB) physical, entry point in the higher half.
"""

import argparse
import struct
import sys

SECTOR = 512
STAGE2_MAGIC = 0x32534C48  # "HLS2"
STAGE2_MAX_SECTORS = 127   # single INT 13h read from stage 1
KERNEL_PADDR_MIN = 0x100000
KERNEL_PADDR_MAX = 0x1000000  # staging area; loads must stay below it
KERNEL_VMA = 0xFFFFFFFF80000000

ELF_MAGIC = b"\x7fELF"
ELFCLASS64 = 2
ELFDATA2LSB = 1
ET_EXEC = 2
EM_X86_64 = 62
PT_LOAD = 1


def die(msg: str) -> None:
    print(f"mkimage: error: {msg}", file=sys.stderr)
    sys.exit(1)


def sectors(nbytes: int) -> int:
    return (nbytes + SECTOR - 1) // SECTOR


def pad_to_sector(data: bytes) -> bytes:
    return data + b"\x00" * (sectors(len(data)) * SECTOR - len(data))


def find_marker(buf: bytes, marker: bytes, what: str) -> int:
    pos = buf.find(marker)
    if pos < 0:
        die(f"{what}: marker {marker!r} not found")
    if buf.find(marker, pos + 1) >= 0:
        die(f"{what}: marker {marker!r} is not unique")
    return pos


def validate_kernel(elf: bytes) -> None:
    if len(elf) < 64:
        die("kernel: file too small to be an ELF")
    if elf[:4] != ELF_MAGIC:
        die("kernel: bad ELF magic")
    if elf[4] != ELFCLASS64:
        die("kernel: not a 64-bit ELF")
    if elf[5] != ELFDATA2LSB:
        die("kernel: not little-endian")
    (e_type,) = struct.unpack_from("<H", elf, 0x10)
    (e_machine,) = struct.unpack_from("<H", elf, 0x12)
    if e_type != ET_EXEC:
        die(f"kernel: e_type {e_type}, expected ET_EXEC ({ET_EXEC})")
    if e_machine != EM_X86_64:
        die(f"kernel: e_machine {e_machine}, expected EM_X86_64 ({EM_X86_64})")

    (e_entry,) = struct.unpack_from("<Q", elf, 0x18)
    (e_phoff,) = struct.unpack_from("<Q", elf, 0x20)
    (e_phentsize,) = struct.unpack_from("<H", elf, 0x36)
    (e_phnum,) = struct.unpack_from("<H", elf, 0x38)

    if e_entry < KERNEL_VMA:
        die(f"kernel: entry {e_entry:#x} is not in the higher half")
    if e_phnum == 0:
        die("kernel: no program headers")

    loads = 0
    for i in range(e_phnum):
        off = e_phoff + i * e_phentsize
        if off + 0x38 > len(elf):
            die(f"kernel: program header {i} out of bounds")
        (p_type,) = struct.unpack_from("<I", elf, off)
        if p_type != PT_LOAD:
            continue
        loads += 1
        (p_offset,) = struct.unpack_from("<Q", elf, off + 0x08)
        (p_paddr,) = struct.unpack_from("<Q", elf, off + 0x18)
        (p_filesz,) = struct.unpack_from("<Q", elf, off + 0x20)
        (p_memsz,) = struct.unpack_from("<Q", elf, off + 0x28)
        if p_filesz > p_memsz:
            die(f"kernel: segment {i}: filesz {p_filesz:#x} > memsz {p_memsz:#x}")
        if p_offset + p_filesz > len(elf):
            die(f"kernel: segment {i}: file range out of bounds")
        if p_paddr < KERNEL_PADDR_MIN:
            die(f"kernel: segment {i}: p_paddr {p_paddr:#x} below {KERNEL_PADDR_MIN:#x}")
        if p_paddr + p_memsz > KERNEL_PADDR_MAX:
            die(
                f"kernel: segment {i}: end {p_paddr + p_memsz:#x} overlaps the "
                f"staging area at {KERNEL_PADDR_MAX:#x}"
            )
    if loads == 0:
        die("kernel: no PT_LOAD segments")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--stage1", required=True)
    ap.add_argument("--stage2", required=True)
    ap.add_argument("--kernel", required=True)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    with open(args.stage1, "rb") as f:
        stage1 = bytearray(f.read())
    with open(args.stage2, "rb") as f:
        stage2 = bytearray(f.read())
    with open(args.kernel, "rb") as f:
        kernel = f.read()

    # -- stage 1 checks + patch ------------------------------------------
    if len(stage1) != SECTOR:
        die(f"stage1: size {len(stage1)}, expected exactly {SECTOR}")
    if stage1[510:512] != b"\x55\xaa":
        die("stage1: missing 0xAA55 boot signature")

    stage2_sectors = sectors(len(stage2))
    if stage2_sectors == 0:
        die("stage2: empty")
    if stage2_sectors > STAGE2_MAX_SECTORS:
        die(f"stage2: {stage2_sectors} sectors, limit {STAGE2_MAX_SECTORS}")

    hb1 = find_marker(stage1, b"HB1\x00", "stage1")
    # The two bytes after the marker begin the DAP: size 0x10, reserved 0.
    if stage1[hb1 + 4] != 0x10 or stage1[hb1 + 5] != 0x00:
        die("stage1: HB1 marker is not followed by a DAP")
    struct.pack_into("<H", stage1, hb1 + 6, stage2_sectors)

    # -- stage 2 checks + patch ------------------------------------------
    (s2_magic,) = struct.unpack_from("<I", stage2, 0)
    if s2_magic != STAGE2_MAGIC:
        die(f"stage2: magic {s2_magic:#x}, expected {STAGE2_MAGIC:#x}")

    kernel_lba = 1 + stage2_sectors
    kernel_sectors = sectors(len(kernel))
    if kernel_sectors == 0:
        die("kernel: empty")

    hb2 = find_marker(stage2, b"HB2\x00", "stage2")
    struct.pack_into("<Q", stage2, hb2 + 4, kernel_lba)
    struct.pack_into("<I", stage2, hb2 + 12, kernel_sectors)

    # -- kernel checks -----------------------------------------------------
    validate_kernel(kernel)

    # -- assemble ----------------------------------------------------------
    image = bytes(stage1) + pad_to_sector(bytes(stage2)) + pad_to_sector(kernel)
    # Round the image up to a whole MiB so it presents as a sane disk.
    mib = 1 << 20
    image += b"\x00" * (-len(image) % mib)

    with open(args.out, "wb") as f:
        f.write(image)

    print(
        f"mkimage: {args.out}: {len(image) // mib} MiB "
        f"(stage2 {stage2_sectors} sectors, kernel {kernel_sectors} sectors "
        f"at LBA {kernel_lba})"
    )


if __name__ == "__main__":
    main()
