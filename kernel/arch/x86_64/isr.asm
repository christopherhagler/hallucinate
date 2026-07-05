; isr.asm - interrupt service routine stubs.
;
; One stub per vector normalizes the stack to a struct trapframe
; (see trap.h) and jumps to a common path that saves all GPRs and
; calls trap_dispatch(). Vectors where the CPU pushes an error code
; (8, 10-14, 17, 21, 29, 30) skip the dummy-error push.
;
; Alignment: the CPU aligns RSP to 16 before pushing the 5-word frame
; (+1 for an error code), so RSP % 16 == 8 after the vector push in
; both cases; 15 GPR pushes later RSP is 16-aligned at the call, as
; the SysV ABI requires.

BITS 64
SECTION .text

extern trap_dispatch

%macro ISR_STUB 1
isr%1:
%if (%1 == 8) || ((%1 >= 10) && (%1 <= 14)) || (%1 == 17) || (%1 == 21) || (%1 == 29) || (%1 == 30)
    push qword %1                 ; error code already pushed by the CPU
%else
    push qword 0                  ; dummy error code
    push qword %1
%endif
    jmp isr_common
%endmacro

%assign v 0
%rep 256
    ISR_STUB v
%assign v v+1
%endrep

isr_common:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    mov rdi, rsp                  ; struct trapframe *
    cld                           ; ABI requires DF clear
    call trap_dispatch

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    add rsp, 16                   ; vector + error code
    iretq

SECTION .rodata
global isr_stub_table
isr_stub_table:
%assign v 0
%rep 256
    dq isr%+v
%assign v v+1
%endrep
