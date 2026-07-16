/*
 * linker.ld - Hallucinate OS kernel layout.
 *
 * Higher-half kernel: virtual addresses at KERNEL_VMA + 1 MiB, physical load
 * addresses (p_paddr, used by stage 2) at 1 MiB. Keep in sync with
 * docs/book/appendix-e-boot-protocol.md and kernel/include/memlayout.h.
 */

ENTRY(_start)

KERNEL_VMA = 0xffffffff80000000;

SECTIONS
{
    . = KERNEL_VMA + 0x100000;

    _kernel_start = .;

    _text_start = .;
    .text : AT(ADDR(.text) - KERNEL_VMA)
    {
        *(.text .text.*)
    }
    _text_end = .;

    . = ALIGN(0x1000);
    _rodata_start = .;
    .rodata : AT(ADDR(.rodata) - KERNEL_VMA)
    {
        *(.rodata .rodata.*)
    }
    _rodata_end = .;

    . = ALIGN(0x1000);
    _data_start = .;
    .data : AT(ADDR(.data) - KERNEL_VMA)
    {
        *(.data .data.*)
    }

    . = ALIGN(0x1000);
    .bss : AT(ADDR(.bss) - KERNEL_VMA)
    {
        *(.bss .bss.*)
        *(COMMON)
    }
    _data_end = .;

    _kernel_end = .;

    /DISCARD/ :
    {
        *(.eh_frame)
        *(.comment)
        *(.note .note.*)
    }
}
