/*
 * user.ld - link script for userspace programs.
 *
 * Static ET_EXEC at 0x400000 with three page-aligned PT_LOAD
 * segments so the kernel's per-segment W^X policy has real teeth:
 * text R+X, rodata R, data/bss R+W. The ELF headers are not mapped
 * (no FILEHDR/PHDRS placement) — the kernel loader reads them from
 * the embedded image, not from user memory.
 */
ENTRY(_start)

PHDRS {
    text   PT_LOAD FLAGS(0x5); /* R X */
    rodata PT_LOAD FLAGS(0x4); /* R   */
    data   PT_LOAD FLAGS(0x6); /* RW  */
}

SECTIONS {
    . = 0x400000;

    .text : {
        *(.text .text.*)
    } :text

    . = ALIGN(4096);
    .rodata : {
        *(.rodata .rodata.*)
    } :rodata

    . = ALIGN(4096);
    .data : {
        *(.data .data.*)
    } :data

    .bss : {
        *(.bss .bss.*)
        *(COMMON)
    } :data

    /DISCARD/ : {
        *(.comment .note.* .eh_frame .eh_frame_hdr)
    }
}
