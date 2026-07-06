; ctx.asm - kernel thread context switch.
;
; A thread off the CPU is entirely described by its saved RSP: the
; System V callee-saved registers plus a return address live on its
; own kernel stack in the frame laid out below. Caller-saved state
; needs no help — ctx_switch is an ordinary C-visible call, so the
; compiler has already spilled anything it cares about.
;
; Frame at [saved rsp], ascending:
;   +0  r15   +8  r14   +16 r13   +24 r12   +32 rbx   +40 rbp
;   +48 return address
;
; New threads get a hand-built frame (sched.c) whose return address is
; thread_entry_trampoline with r12 = entry function, r13 = argument.

bits 64
section .text

; void ctx_switch(uint64_t *save_rsp, uint64_t load_rsp)
;
; Must be called with interrupts disabled: a timer interrupt between
; the stack switch and return would run on a half-switched thread.
global ctx_switch
ctx_switch:
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15
    mov [rdi], rsp      ; park the outgoing thread
    mov rsp, rsi        ; adopt the incoming thread's stack
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    ret

; First code a new thread ever runs, entered by ctx_switch's ret.
; Interrupts are off (scheduler invariant); turn them on before the
; thread body. thread_exit never returns, so falling off the end of
; the entry function lands there and terminates the thread cleanly.
extern thread_exit
global thread_entry_trampoline
thread_entry_trampoline:
    sti
    mov rdi, r13        ; argument
    call r12            ; entry(argument); rsp is 16-aligned here
    call thread_exit
    ; thread_exit is noreturn; nothing to fall into.
