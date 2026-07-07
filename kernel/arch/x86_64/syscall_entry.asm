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

    ; Build a struct syscall_frame (syscall.h) — the complete user
    ; register state. The ABI promises userspace that only rax
    ; (result), rcx, and r11 (hardware-clobbered) change across a
    ; syscall, so everything is restored from here on the way out;
    ; fork() copies this frame to clone the user context. Kernel
    ; stack tops are 16-aligned; sixteen pushes keep RSP 16-aligned
    ; at the call below, as the ABI requires.
    push qword [rel saved_user_rsp] ; frame.rsp
    push rcx                    ; frame.rip
    push r11                    ; frame.rflags
    push rax                    ; frame.rax: nr in, return value out
    push rdi
    push rsi
    push rdx
    push r10
    push r8
    push r9
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15

    ; Re-permit interrupts: syscalls may block or be preempted.
    sti

    mov rdi, rsp                ; syscall_dispatch(frame)
    cld
    call syscall_dispatch

    ; Interrupts off for the return: after the stack switches back to
    ; user memory, a kernel-stack-less interrupt window must not exist.
    cli
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx
    pop r9
    pop r8
    pop r10
    pop rdx
    pop rsi
    pop rdi
    pop rax                     ; return value (dispatch wrote frame.rax)
    pop r11                     ; user rflags (restored by sysret)
    pop rcx                     ; user rip
    pop rsp                     ; user stack
    o64 sysret

; void user_frame_enter(const struct syscall_frame *f)
;
; Enter ring 3 with the complete register state described by *f: a
; process's first entry (zeroed frame, rip/rsp/rflags set) and fork's
; child resuming at the parent's syscall return point are the same
; operation. rcx/r11 mirror rip/rflags, exactly as the sysret return
; path would leave them. The iretq lands in user code atomically with
; the frame's interrupt state (IF is always set). Never returns.
;
; Selectors from gdt.h: user data 0x20 | RPL 3 = 0x23,
;                       user code 0x28 | RPL 3 = 0x2B.
global user_frame_enter
user_frame_enter:
    cli
    push qword 0x23             ; ss (user data | RPL 3)
    push qword [rdi + (15 * 8)] ; frame.rsp
    push qword [rdi + (13 * 8)] ; frame.rflags (IF set: was running user)
    push qword 0x2B             ; cs (user code | RPL 3)
    push qword [rdi + (14 * 8)] ; frame.rip

    mov r15, [rdi + (0 * 8)]
    mov r14, [rdi + (1 * 8)]
    mov r13, [rdi + (2 * 8)]
    mov r12, [rdi + (3 * 8)]
    mov rbp, [rdi + (4 * 8)]
    mov rbx, [rdi + (5 * 8)]
    mov r9,  [rdi + (6 * 8)]
    mov r8,  [rdi + (7 * 8)]
    mov r10, [rdi + (8 * 8)]
    mov rdx, [rdi + (9 * 8)]
    mov rsi, [rdi + (10 * 8)]
    mov rax, [rdi + (12 * 8)]
    mov rcx, [rdi + (14 * 8)]   ; as sysret would: rcx = rip
    mov r11, [rdi + (13 * 8)]   ;                  r11 = rflags
    mov rdi, [rdi + (11 * 8)]   ; last: clobbers the frame pointer
    iretq

section .data
align 8
global syscall_kstack
syscall_kstack:  dq 0
saved_user_rsp:  dq 0
