; init.asm - the first userspace program: a flat binary embedded in
; the kernel image (kernel/user_blob.asm) and mapped at 0x400000.
;
; Exercises the whole native syscall ABI surface and reports through
; its exit status: 0 only if write() returned the full length,
; getpid() returned init's PID, and an unimplemented syscall returned
; -ENOSYS. The kernel prints the status, and the boot integration
; test asserts on it.

bits 64
org 0x400000

_start:
    ; write(1, msg, msg_len) == msg_len
    mov eax, 1                  ; SYS_write
    mov edi, 1                  ; stdout
    lea rsi, [rel msg]
    mov edx, msg_len
    syscall
    cmp rax, msg_len
    jne .fail

    ; getpid() == 1
    mov eax, 39                 ; SYS_getpid
    syscall
    cmp rax, 1
    jne .fail

    ; syscall 999 does not exist -> -ENOSYS (-38)
    mov eax, 999
    syscall
    cmp rax, -38
    jne .fail

    xor edi, edi                ; status 0: everything checked out
    jmp .exit
.fail:
    mov edi, 1
.exit:
    mov eax, 60                 ; SYS_exit
    syscall
    jmp .exit                   ; unreachable: exit does not return

msg:     db "hello from ring 3", 10
msg_len  equ $ - msg
