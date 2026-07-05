/*
 * bootinfo.h - the boot protocol contract between stage 2 and the kernel.
 *
 * Stage 2 fills a `struct bootinfo` at physical BOOTINFO_PHYS and enters the
 * kernel with RDI holding that physical address. Any change here is a boot
 * protocol change: bump BOOTINFO_VERSION and update docs/boot-protocol.md
 * and boot/stage2.asm together.
 */
#ifndef HL_BOOTINFO_H
#define HL_BOOTINFO_H

#include <stddef.h>
#include <stdint.h>

#include <compiler.h>

#define BOOTINFO_MAGIC   0x4E434C48u /* "HLCN" */
#define BOOTINFO_VERSION 1
#define BOOTINFO_PHYS    0x6000u
#define E820_MAX_ENTRIES 64

/* E820 memory range types (ACPI-defined). */
#define E820_TYPE_USABLE       1
#define E820_TYPE_RESERVED     2
#define E820_TYPE_ACPI_RECLAIM 3
#define E820_TYPE_ACPI_NVS     4
#define E820_TYPE_BAD          5

struct e820_entry {
    uint64_t base;
    uint64_t len;
    uint32_t type;
    uint32_t attr; /* ACPI 3.0 extended attributes; bit 0 = entry valid */
} PACKED;

struct bootinfo {
    uint32_t magic;     /* BOOTINFO_MAGIC */
    uint16_t version;   /* BOOTINFO_VERSION */
    uint8_t boot_drive; /* BIOS drive number the system booted from */
    uint8_t reserved0;
    uint32_t e820_count; /* number of valid entries in e820[] */
    uint32_t reserved1;
    struct e820_entry e820[E820_MAX_ENTRIES];
} PACKED;

_Static_assert(sizeof(struct e820_entry) == 24, "e820 entry layout is part of the boot ABI");
_Static_assert(offsetof(struct bootinfo, e820) == 16, "bootinfo layout is part of the boot ABI");

#endif /* HL_BOOTINFO_H */
