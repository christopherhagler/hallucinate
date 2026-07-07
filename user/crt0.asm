; crt0.asm - userspace C runtime entry.
;
; The kernel enters ring 3 with RSP pointing at the SysV process
; stack: [argc][argv...][NULL][envp...][NULL][auxv]. Pick up
; argc/argv for main, establish the ABI environment (zero frame
; pointer to terminate unwinds, 16-byte stack alignment at the call
; site), run main, and exit with its return value.

bits 64
section .text

global _start
extern main

_start:
    xor ebp, ebp
    mov rdi, [rsp]              ; argc
    lea rsi, [rsp + 8]          ; argv
    and rsp, -16
    call main
    mov edi, eax                ; exit(main())
    mov eax, 60                 ; SYS_exit
.exit:
    syscall
    jmp .exit                   ; unreachable: exit does not return
