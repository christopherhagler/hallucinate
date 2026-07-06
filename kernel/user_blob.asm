; user_blob.asm - embeds the userspace init program (a flat binary
; built from user/init.asm) into kernel .rodata. The kernel copies it
; into a fresh user address space at boot; see kernel/proc/process.c.
;
; The include path for init.bin is supplied by the Makefile
; (nasm -i build/user/).

bits 64
section .rodata

global user_init_blob
global user_init_blob_end

user_init_blob:
    incbin "init.bin"
user_init_blob_end:
