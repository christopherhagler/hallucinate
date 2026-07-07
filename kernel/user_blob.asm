; user_blob.asm - embeds the userspace programs (static ELF64
; executables built from user/) into kernel .rodata. They form the
; built-in program table in kernel/proc/process.c — the program
; source for init and execve until the VFS lands (Phase 5).
;
; The include path for the images is supplied by the Makefile
; (nasm -i build/user/).

bits 64
section .rodata

global user_init_blob
global user_init_blob_end
global user_hello_blob
global user_hello_blob_end

user_init_blob:
    incbin "init.elf"
user_init_blob_end:

user_hello_blob:
    incbin "hello.elf"
user_hello_blob_end:
