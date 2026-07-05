; stage2.asm - Hallucinate OS stage-2 loader.
;
; Loaded by stage 1 at 0000:7E00, entered at 0000:7E04 in real mode with
; DL = boot drive. Responsibilities, in order:
;
;   1. Enable the A20 line (BIOS, then port 0x92, then keyboard controller;
;      verified after each attempt).
;   2. Collect the E820 memory map into the bootinfo block at 0x6000.
;   3. Read the kernel ELF image from disk (LBA/sector count patched into the
;      "HB2" block by mkimage) into the staging area at 16 MiB, using INT 13h
;      LBA reads into a low buffer plus unreal-mode copies upward.
;   4. Enter 32-bit protected mode and build the initial page tables:
;      the first 1 GiB of physical memory mapped both identity and at
;      0xffffffff80000000, using 2 MiB pages.
;   5. Enable long mode, parse the staged ELF64, copy PT_LOAD segments to
;      their physical load addresses, and jump to the entry point with
;      RDI = physical address of bootinfo (see docs/boot-protocol.md).
;
; Boot-time physical memory layout (see docs/boot-protocol.md):
;   0x00500          A20 wraparound test scratch byte
;   0x06000          bootinfo block (magic, e820 map, ...)
;   0x07C00          stage 1 + stack (grows down from 0x7C00)
;   0x07E00          this stage
;   0x20000          32 KiB disk read buffer
;   0x70000-0x73FFF  initial page tables (PML4, 2x PDPT, PD)
;   0x100000         kernel physical load region (from ELF p_paddr)
;   0x1000000        kernel ELF staging area

BITS 16
ORG 0x7E00

%define STAGE2_MAGIC   0x32534C48    ; "HLS2"
%define BOOTINFO       0x6000
%define BOOTINFO_MAGIC 0x4E434C48    ; "HLCN"
%define E820_ENTRIES   (BOOTINFO + 16)
%define E820_MAX       64
%define READ_BUF       0x20000       ; physical address of disk read buffer
%define READ_BUF_SEG   0x2000
%define CHUNK_SECTORS  64            ; 32 KiB per INT 13h read
%define PML4_ADDR      0x70000
%define PDPT_LO_ADDR   0x71000
%define PDPT_HI_ADDR   0x72000
%define PD_ADDR        0x73000
%define STAGING        0x1000000     ; kernel ELF staging (16 MiB)
%define KERNEL_MIN     0x100000      ; lowest allowed p_paddr (1 MiB)

%define SEL_CODE32     0x08
%define SEL_DATA       0x10
%define SEL_CODE64     0x18

    dd STAGE2_MAGIC                  ; verified by stage 1; entry is +4

entry16:
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    cld
    mov [boot_drive], dl

    mov si, msg_banner
    call print16

    ; ------------------------------------------------------------- A20 ----
    call a20_test
    jnz .a20_ok
    mov ax, 0x2401                   ; BIOS: enable A20
    int 0x15
    call a20_test
    jnz .a20_ok
    in al, 0x92                      ; fast A20 gate
    or al, 0x02
    and al, 0xFE                     ; never touch bit 0 (CPU reset)
    out 0x92, al
    call a20_test
    jnz .a20_ok
    call a20_kbc                     ; keyboard controller method
    call a20_test
    jnz .a20_ok
    mov si, msg_err_a20
    jmp fail16
.a20_ok:

    ; ------------------------------------------------------------ E820 ----
    mov di, E820_ENTRIES
    xor ebx, ebx                     ; continuation value
    xor bp, bp                       ; entry count
.e820_loop:
    mov eax, 0xE820
    mov edx, 0x534D4150              ; 'SMAP'
    mov ecx, 24
    mov dword [di + 20], 1           ; ACPI3 ext attr default: entry valid
    int 0x15
    jc .e820_end                     ; carry: end of map (or unsupported)
    cmp eax, 0x534D4150
    jne .e820_fail
    inc bp
    cmp bp, E820_MAX
    jae .e820_done
    add di, 24
    test ebx, ebx
    jnz .e820_loop
    jmp .e820_done
.e820_end:
    test bp, bp
    jnz .e820_done
.e820_fail:
    mov si, msg_err_e820
    jmp fail16
.e820_done:

    ; ------------------------------------------------- bootinfo header ----
    mov dword [BOOTINFO + 0], BOOTINFO_MAGIC
    mov word  [BOOTINFO + 4], 1      ; boot protocol version
    mov al, [boot_drive]
    mov [BOOTINFO + 6], al
    mov byte  [BOOTINFO + 7], 0
    movzx eax, bp
    mov [BOOTINFO + 8], eax          ; e820_count
    mov dword [BOOTINFO + 12], 0

    ; ------------------------------------------------- load kernel ELF ----
    mov eax, [kernel_sectors]
    test eax, eax
    jz .k_fail
    mov [remaining], eax
    mov eax, [kernel_lba]
    mov [cur_lba], eax
    mov eax, [kernel_lba + 4]
    mov [cur_lba + 4], eax
    mov dword [dest], STAGING

.load_loop:
    mov eax, [remaining]
    test eax, eax
    jz .load_done
    mov ebx, CHUNK_SECTORS
    cmp eax, ebx
    jae .have_chunk
    mov ebx, eax
.have_chunk:                         ; bx = sectors this round
    mov [dap_count], bx
    mov eax, [cur_lba]
    mov [dap_lba], eax
    mov eax, [cur_lba + 4]
    mov [dap_lba + 4], eax

    mov cx, 3                        ; read retries
.read_retry:
    push bx
    push cx
    mov si, dap
    mov dl, [boot_drive]
    mov ah, 0x42
    int 0x13
    pop cx
    pop bx
    jnc .read_ok
    push bx
    push cx
    xor ah, ah                       ; reset disk system
    mov dl, [boot_drive]
    int 0x13
    pop cx
    pop bx
    loop .read_retry
.k_fail:
    mov si, msg_err_disk
    jmp fail16
.read_ok:
    push bx
    mov ah, 0x0E                     ; progress dot
    mov al, '.'
    xor bx, bx
    int 0x10
    pop bx

    ; Copy the chunk above 1 MiB. INT 13h may have bounced through protected
    ; mode and reset the cached segment limits, so re-enter unreal mode for
    ; every copy. Interrupts stay off from here until after the copy.
    call enter_unreal
    mov esi, READ_BUF
    mov edi, [dest]
    movzx ecx, bx
    shl ecx, 7                       ; sectors * 512 / 4 dwords
    a32 rep movsd
    sti

    movzx eax, bx
    shl eax, 9                       ; sectors * 512 bytes
    add [dest], eax
    movzx eax, bx
    add [cur_lba], eax
    adc dword [cur_lba + 4], 0
    movzx eax, bx
    sub [remaining], eax
    jmp .load_loop
.load_done:

    mov si, msg_newline
    call print16

    ; -------------------------------------------- protected mode entry ----
    cli
    lgdt [gdt_desc]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp SEL_CODE32:pm32_entry

; --------------------------------------------------------------------------
; 16-bit helpers
; --------------------------------------------------------------------------

; print16: write NUL-terminated string at DS:SI via BIOS teletype.
print16:
    push ax
    push bx
    mov ah, 0x0E
    xor bx, bx
.next:
    lodsb
    test al, al
    jz .done
    int 0x10
    jmp .next
.done:
    pop bx
    pop ax
    ret

; fail16: print message at DS:SI, then halt forever.
fail16:
    call print16
.halt:
    cli
    hlt
    jmp .halt

; a20_test: ZF clear (jnz) if the A20 line is enabled, AL = 1/0 accordingly.
; Writes differing bytes to 0x000500 and 0x100500 and checks for wraparound.
; Clobbers AX and DX; preserves segments, index registers, and flags' IF.
a20_test:
    push ds
    push es
    push si
    push di
    pushf
    cli
    xor ax, ax
    mov es, ax
    mov di, 0x0500                   ; 0000:0500 = 0x000500
    mov ax, 0xFFFF
    mov ds, ax
    mov si, 0x0510                   ; FFFF:0510 = 0x100500
    mov dl, [es:di]                  ; preserve original bytes
    mov dh, [si]
    mov byte [es:di], 0x00
    mov byte [si], 0xFF
    mov al, [es:di]                  ; reads 0xFF iff the write wrapped
    mov [si], dh                     ; restore
    mov [es:di], dl
    cmp al, 0xFF
    mov al, 1                        ; assume enabled (mov keeps flags)
    jne .done
    xor al, al                       ; wrapped: disabled
.done:
    popf
    pop di
    pop si
    pop es
    pop ds
    test al, al                      ; ZF clear iff enabled
    ret

; a20_kbc: enable A20 through the 8042 keyboard controller.
a20_kbc:
    call .wait_in
    mov al, 0xAD                     ; disable keyboard
    out 0x64, al
    call .wait_in
    mov al, 0xD0                     ; read output port
    out 0x64, al
    call .wait_out
    in al, 0x60
    push ax
    call .wait_in
    mov al, 0xD1                     ; write output port
    out 0x64, al
    call .wait_in
    pop ax
    or al, 0x02                      ; A20 bit
    out 0x60, al
    call .wait_in
    mov al, 0xAE                     ; re-enable keyboard
    out 0x64, al
    call .wait_in
    ret
; Bounded waits: give up after 64K polls so a machine without a working 8042
; falls through to the (failing) A20 test instead of hanging here.
.wait_in:                            ; wait for input buffer empty
    push cx
    xor cx, cx
.wi_poll:
    in al, 0x64
    test al, 0x02
    jz .wi_done
    loop .wi_poll
.wi_done:
    pop cx
    ret
.wait_out:                           ; wait for output buffer full
    push cx
    xor cx, cx
.wo_poll:
    in al, 0x64
    test al, 0x01
    jnz .wo_done
    loop .wo_poll
.wo_done:
    pop cx
    ret

; enter_unreal: leave DS and ES with 4 GiB limits, base 0. Returns with
; interrupts disabled; the caller re-enables them when safe.
enter_unreal:
    cli
    push ds
    push es
    lgdt [gdt_desc]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp short .pm                    ; flush prefetch
.pm:
    mov ax, SEL_DATA
    mov ds, ax
    mov es, ax
    mov eax, cr0
    and al, 0xFE
    mov cr0, eax
    jmp short .rm
.rm:
    pop es                           ; real-mode bases restored,
    pop ds                           ; cached 4 GiB limits survive
    ret

; --------------------------------------------------------------------------
; 32-bit: initial page tables, long mode enable
; --------------------------------------------------------------------------
BITS 32
pm32_entry:
    mov ax, SEL_DATA
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax
    mov esp, 0x7C00

    ; Clear the four page-table pages.
    xor eax, eax
    mov edi, PML4_ADDR
    mov ecx, 4 * 4096 / 4
    rep stosd

    ; Top levels: first 1 GiB mapped identity and at 0xffffffff80000000.
    mov dword [PML4_ADDR], PDPT_LO_ADDR | 0x03            ; present | write
    mov dword [PML4_ADDR + 511 * 8], PDPT_HI_ADDR | 0x03
    mov dword [PDPT_LO_ADDR], PD_ADDR | 0x03
    mov dword [PDPT_HI_ADDR + 510 * 8], PD_ADDR | 0x03

    ; Page directory: 512 x 2 MiB pages covering physical 0..1 GiB.
    mov edi, PD_ADDR
    mov eax, 0x83                    ; present | write | page size (2 MiB)
    mov ecx, 512
.pd_loop:
    mov [edi], eax
    mov dword [edi + 4], 0
    add eax, 0x200000
    add edi, 8
    loop .pd_loop

    mov eax, cr4                     ; PAE
    or eax, 1 << 5
    mov cr4, eax
    mov eax, PML4_ADDR
    mov cr3, eax
    mov ecx, 0xC0000080              ; EFER.LME
    rdmsr
    or eax, 1 << 8
    wrmsr
    mov eax, cr0                     ; paging on -> long mode active
    or eax, 1 << 31
    mov cr0, eax
    jmp SEL_CODE64:lm64_entry

; --------------------------------------------------------------------------
; 64-bit: ELF64 load and kernel handoff
; --------------------------------------------------------------------------
BITS 64
lm64_entry:
    mov ax, SEL_DATA
    mov ds, ax
    mov es, ax
    mov ss, ax
    xor eax, eax
    mov fs, ax
    mov gs, ax
    mov rsp, 0x7C00

    mov rbp, STAGING
    cmp dword [rbp], 0x464C457F      ; "\x7fELF"
    jne err64_elf
    cmp byte [rbp + 4], 2            ; ELFCLASS64
    jne err64_elf
    cmp byte [rbp + 5], 1            ; little-endian
    jne err64_elf
    cmp word [rbp + 0x12], 62        ; EM_X86_64
    jne err64_elf

    movzx r8, word [rbp + 0x36]      ; e_phentsize
    movzx r9, word [rbp + 0x38]      ; e_phnum
    mov rbx, [rbp + 0x20]            ; e_phoff
    add rbx, rbp

.ph_loop:
    test r9, r9
    jz .ph_done
    cmp dword [rbx], 1               ; PT_LOAD
    jne .ph_next

    mov rsi, [rbx + 0x08]            ; p_offset
    add rsi, rbp
    mov rdi, [rbx + 0x18]            ; p_paddr
    mov rcx, [rbx + 0x20]            ; p_filesz
    mov rdx, [rbx + 0x28]            ; p_memsz

    cmp rcx, rdx                     ; filesz <= memsz
    ja err64_elf
    cmp rdi, KERNEL_MIN              ; never below 1 MiB
    jb err64_elf
    mov rax, rdi
    add rax, rdx
    jc err64_elf
    cmp rax, STAGING                 ; never into the staging area
    ja err64_elf

    sub rdx, rcx                     ; rdx = bytes of BSS to zero
    rep movsb
    mov rcx, rdx
    xor eax, eax
    rep stosb

.ph_next:
    add rbx, r8
    dec r9
    jmp .ph_loop

.ph_done:
    mov rax, [rbp + 0x18]            ; e_entry
    mov rdx, 0xFFFFFF8000000000
    cmp rax, rdx                     ; entry must be higher-half
    jb err64_elf

    mov edi, BOOTINFO                ; RDI = bootinfo physical address
    jmp rax

; err64_elf: report a bad kernel image on the VGA text screen and halt.
err64_elf:
    mov rdi, 0xB8000
    mov rsi, msg_err_elf64
.next:
    lodsb
    test al, al
    jz .halt
    mov ah, 0x4F                     ; white on red
    stosw
    jmp .next
.halt:
    cli
    hlt
    jmp .halt

; --------------------------------------------------------------------------
; Data
; --------------------------------------------------------------------------

align 8
gdt:
    dq 0
    dq 0x00CF9A000000FFFF            ; 0x08: 32-bit code, flat 4 GiB
    dq 0x00CF92000000FFFF            ; 0x10: data, flat 4 GiB
    dq 0x00AF9A000000FFFF            ; 0x18: 64-bit code
gdt_desc:
    dw gdt_desc - gdt - 1
    dd gdt

; Build-time patch block ("HB2" marker; see tools/mkimage.py).
align 4
hb2_marker      db "HB2", 0
kernel_lba      dq 0                 ; PATCHED: first LBA of kernel image
kernel_sectors  dd 0                 ; PATCHED: kernel image size in sectors

; Disk Address Packet for kernel reads (rebuilt each chunk).
align 4
dap:
    db 0x10, 0
dap_count:
    dw 0
    dw 0                             ; buffer offset
    dw READ_BUF_SEG                  ; buffer segment
dap_lba:
    dq 0

boot_drive  db 0
cur_lba     dq 0
remaining   dd 0
dest        dd 0

; All fatal boot errors contain "ERR:" — the test harness keys on it.
msg_banner    db "HB2: stage2", 0
msg_newline   db 13, 10, 0
msg_err_a20   db 13, 10, "HB2 ERR: A20 enable failed", 13, 10, 0
msg_err_e820  db 13, 10, "HB2 ERR: E820 memory map failed", 13, 10, 0
msg_err_disk  db 13, 10, "HB2 ERR: kernel read failed", 13, 10, 0
msg_err_elf64 db "HB2 ERR: bad kernel ELF", 0
