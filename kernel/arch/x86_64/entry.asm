; entry.asm - x86_64 kernel entry point.
;
; Stage 2 jumps here in 64-bit long mode with:
;   - RDI = physical address of the bootinfo block
;   - paging on: first 1 GiB mapped identity and at 0xffffffff80000000
;   - interrupts disabled, no IDT, no TSS
;
; This stub only establishes the kernel stack and calls kmain. kmain never
; returns; the trailing halt loop is a backstop.

BITS 64

section .text
global _start
extern kmain

_start:
    cli
    cld
    lea rsp, [rel kstack_top]
    xor ebp, ebp
    call kmain
.hang:
    cli
    hlt
    jmp .hang

section .bss
align 16
kstack:
    resb 16384                       ; 16 KiB boot stack
kstack_top:
