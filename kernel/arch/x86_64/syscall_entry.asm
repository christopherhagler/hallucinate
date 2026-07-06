; syscall_entry.asm - SYSCALL entry, SYSRET exit, and the iretq drop
; into ring 3.
;
; SYSCALL leaves us on the *user* stack with rcx = user rip and
; r11 = user rflags; SFMASK cleared IF, so nothing can interrupt until
; we stand on a kernel stack. Single CPU: the current thread's kernel
; stack top lives in one global (syscall_kstack), maintained by the
; scheduler on every context switch — the swapgs/per-CPU dance arrives
; with SMP.

bits 64
section .text

extern syscall_dispatch

; Loaded into MSR LSTAR by syscall_init().
global syscall_entry
syscall_entry:
    mov [rel saved_user_rsp], rsp
    mov rsp, [rel syscall_kstack]

    ; Kernel stack tops are 16-aligned; four pushes keep RSP 16-aligned
    ; at the call below, as the ABI requires.
    push qword [rel saved_user_rsp]
    push rcx                    ; user rip
    push r11                    ; user rflags
    push qword 0                ; alignment slot

    ; Re-permit interrupts: syscalls may block or be preempted.
    sti

    ; Marshal (rax=nr, rdi,rsi,rdx,r10) -> C ABI (rdi,rsi,rdx,rcx,r8).
    mov r8, r10
    mov rcx, rdx
    mov rdx, rsi
    mov rsi, rdi
    mov rdi, rax
    cld
    call syscall_dispatch

    ; Interrupts off for the return: after the stack switches back to
    ; user memory, a kernel-stack-less interrupt window must not exist.
    cli
    add rsp, 8                  ; drop the alignment slot
    pop r11                     ; user rflags (restored by sysret)
    pop rcx                     ; user rip
    pop rsp                     ; user stack
    o64 sysret

; void user_enter(uint64_t rip, uint64_t rsp)
;
; First drop into ring 3, used once per process launch; subsequent
; kernel->user returns go through sysret above. Interrupt state is
; carried in the frame (IF set), so the iretq atomically lands in
; user code with interrupts on. Never returns.
;
; Selectors from gdt.h: user data 0x20 | RPL 3 = 0x23,
;                       user code 0x28 | RPL 3 = 0x2B.
global user_enter
user_enter:
    push qword 0x23             ; ss
    push rsi                    ; user rsp
    push qword 0x202            ; rflags: IF | reserved-1
    push qword 0x2B             ; cs
    push rdi                    ; user rip

    ; Nothing of the kernel may leak into ring 3 registers.
    xor eax, eax
    xor ebx, ebx
    xor ecx, ecx
    xor edx, edx
    xor esi, esi
    xor edi, edi
    xor ebp, ebp
    xor r8d, r8d
    xor r9d, r9d
    xor r10d, r10d
    xor r11d, r11d
    xor r12d, r12d
    xor r13d, r13d
    xor r14d, r14d
    xor r15d, r15d
    iretq

section .data
align 8
global syscall_kstack
syscall_kstack:  dq 0
saved_user_rsp:  dq 0
