; stage1.asm - Hallucinate OS stage-1 boot sector (MBR).
;
; The BIOS loads this 512-byte sector to 0000:7C00 and jumps to it with DL set
; to the boot drive. Stage 1 does exactly one job: load stage 2 from the
; sectors immediately following the boot sector (LBA 1..N) to 0000:7E00 using
; INT 13h LBA extensions, verify its magic, and jump to it.
;
; Build-time patching (see tools/mkimage.py and docs/boot-protocol.md):
;   The "HB1" marker below is followed by the Disk Address Packet; mkimage
;   patches the DAP sector count with the true size of stage 2.
;
; Contract with stage 2:
;   - entered by a far jump to 0000:7E04 (0x7E00 holds the magic dword)
;   - DL = BIOS boot drive
;   - real mode, interrupts enabled, stack at 0000:7C00 growing down

BITS 16
ORG 0x7C00

STAGE2_ADDR  equ 0x7E00
STAGE2_MAGIC equ 0x32534C48         ; "HLS2" little-endian

start:
    ; Normalize segments and stack; some BIOSes jump here with CS=07C0.
    cli
    jmp 0x0000:.canon
.canon:
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti
    cld
    mov [boot_drive], dl

    ; Require INT 13h extensions (LBA reads). Present on everything since the
    ; mid-90s and always in QEMU/SeaBIOS; CHS fallback is deliberately not
    ; supported (documented minimum platform requirement).
    mov ah, 0x41
    mov bx, 0x55AA
    int 0x13
    jc  .err_no_ext
    cmp bx, 0xAA55
    jne .err_no_ext

    ; Read stage 2. Retry up to 3 times with a controller reset in between;
    ; real drives can fail transiently after power-on.
    mov cx, 3
.read_retry:
    mov si, dap
    mov dl, [boot_drive]
    mov ah, 0x42
    int 0x13
    jnc .read_ok
    push cx
    xor ah, ah                      ; AH=00: reset disk system
    mov dl, [boot_drive]
    int 0x13
    pop cx
    loop .read_retry
    jmp .err_disk

.read_ok:
    cmp dword [STAGE2_ADDR], STAGE2_MAGIC
    jne .err_bad_stage2
    mov dl, [boot_drive]
    jmp 0x0000:STAGE2_ADDR + 4

.err_no_ext:
    mov si, msg_no_ext
    jmp fail
.err_disk:
    mov si, msg_disk
    jmp fail
.err_bad_stage2:
    mov si, msg_bad2
    jmp fail

; fail: print zero-terminated string at DS:SI via BIOS teletype, then halt.
fail:
    mov ah, 0x0E
    xor bx, bx
.next:
    lodsb
    test al, al
    jz .halt
    int 0x10
    jmp .next
.halt:
    cli
    hlt
    jmp .halt

; All fatal boot errors contain "ERR:" — the test harness keys on it.
msg_no_ext db "HB1 ERR: no LBA ext", 13, 10, 0
msg_disk   db "HB1 ERR: disk read", 13, 10, 0
msg_bad2   db "HB1 ERR: bad stage2", 13, 10, 0

boot_drive db 0

; Build-time patch block: marker + Disk Address Packet for the stage-2 read.
; mkimage locates "HB1\0" and patches the sector count (offset +6 from the
; marker). The count must be <= 127 (INT 13h per-call limit); mkimage asserts.
align 4
hb1_marker db "HB1", 0
dap:
    db 0x10, 0                      ; DAP size, reserved
    dw 0                            ; sector count (PATCHED by mkimage)
    dw STAGE2_ADDR                  ; destination offset
    dw 0                            ; destination segment
    dq 1                            ; starting LBA (stage 2 begins at LBA 1)

times 510 - ($ - $$) db 0
dw 0xAA55
