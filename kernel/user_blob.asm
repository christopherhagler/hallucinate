; user_blob.asm - embeds the userspace init program (a static ELF64
; executable built from user/) into kernel .rodata. The kernel
; validates and loads it into a fresh user address space at boot;
; see kernel/proc/process.c and kernel/proc/elf_load.c.
;
; The include path for init.elf is supplied by the Makefile
; (nasm -i build/user/).

bits 64
section .rodata

global user_init_blob
global user_init_blob_end

user_init_blob:
    incbin "init.elf"
user_init_blob_end:
