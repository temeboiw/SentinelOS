; stage1.S - SentinelOS BIOS/MBR stage-1 boot sector (512 bytes, 0xAA55 signature)
;
; Boot flow:
;   1. Set up stack (SS=0, SP=0x7C00).
;   2. Capture boot drive number from DL (BIOS passes it).
;   3. Load stage-2 from LBA 1, 62 sectors (31 KiB), into RAM at 0x100000.
;      Uses INT 13h AH=02h with CHS conversion from LBA.
;   4. On failure: retry 3 times, then print error string (INT 10h AH=0Eh)
;      and halt.
;   5. Jump to 0x100000 with DL = boot drive.
;
; Layout:
;   0x00-0x1BD : bootstrap code (446 bytes)
;   0x1BE-0x1DD: partition table (4 entries x 16 bytes)
;   0x1FE-0x1FF: signature 0x55, 0xAA
;
; Assumptions:
;   - 512-byte sectors.
;   - Stage-2 lives contiguously at LBA 1..62 on the boot disk.
;   - BIOS INT 13h CHS services are available (no LBA extensions used).
;   - Stage-2 does not switch to protected mode itself; that is stage-2's job.
;   - Real-mode segment:offset 0x0000:0x7C00 load address (BIOS standard).

[BITS 16]
[ORG 0x7C00]

section .text

start:
    ; --- Set up stack ------------------------------------------------
    cli
    xor ax, ax          ; SS = 0
    mov ss, ax
    mov sp, 0x7C00      ; stack grows downward from our load address
    sti

    ; --- Remember boot drive -----------------------------------------
    mov [boot_drive], dl

    ; --- Print loading message ---------------------------------------
    mov si, msg_loading
    call print

    ; --- Load stage-2: LBA 1, 62 sectors, to ES:BX = 0x1000:0x0000 ---
    mov ax, 0x1000      ; segment = 0x1000 -> physical 0x100000
    mov es, ax
    xor bx, bx          ; offset 0
    mov dl, [boot_drive]
    mov cl, 62          ; sector count
    mov al, cl          ; number of sectors to read
    mov ch, 0           ; track/cylinder low bits (set by lba_to_chs)
    mov dh, 0           ; head (set by lba_to_chs)
    mov cl, 1           ; sector (set by lba_to_chs)
    call lba_to_chs     ; converts [lba_start] into ch, dh, cl

.load_sectors:
    mov ah, 0x02        ; INT 13h AH=02h read sectors
    int 0x13
    jc .read_error      ; carry set on error

    ; Success - jump to stage-2
    jmp 0x1000:0x0000

.read_error:
    ; Retry up to 3 times.  Reset the disk system on each retry attempt.
    mov byte [retry_count], 3
.retry:
    dec byte [retry_count]
    js .fail            ; no retries left

    mov ah, 0x00        ; INT 13h AH=00h reset disk system
    mov dl, [boot_drive]
    int 0x13
    jc .retry           ; reset failed too, retry

    ; Recompute CHS and retry the read
    mov ax, 0x1000
    mov es, ax
    xor bx, bx
    mov dl, [boot_drive]
    mov al, 62
    mov cl, 62
    mov ch, 0
    mov dh, 0
    mov cl, 1
    call lba_to_chs
    jmp .load_sectors

.fail:
    mov si, msg_load_fail
    call print
.halt:
    hlt
    jmp .halt

; --- Print null-terminated string at DS:SI via INT 10h AH=0Eh -------
print:
    lodsb
    test al, al
    jz .done
    mov ah, 0x0E
    mov bh, 0x00
    int 0x10
    jmp print
.done:
    ret

; --- Convert LBA (in [lba_start], 32-bit) to CHS ---------------------
; Input:  dl = drive, al = sector count, [lba_start] = LBA
; Output: ch = cylinder low 8 bits, cl = sector (bits 0-5) | cylinder
;                                          high 2 bits (bits 6-7),
;         dh = head
; Geometry assumptions: 63 sectors/track, 2 heads (standard for CHS)
lba_to_chs:
    push ax
    push bx
    push dx

    movzx eax, dword [lba_start]   ; eax = LBA

    ; Standard CHS geometry: 63 sectors/track, 2 heads
    mov bx, 63                     ; sectors per track
    xor dx, dx
    div bx                         ; dx = LBA mod 63 (sector-1), ax = LBA / 63
    inc dl                         ; sector number = remainder + 1
    mov cl, dl                     ; cl = sector

    mov bx, 2                      ; heads
    xor dx, dx
    div bx                         ; dx = (LBA/63) mod 2 (head), ax = cylinder
    mov dh, dl                     ; dh = head
    mov ch, al                     ; ch = cylinder low 8 bits
    ; cylinder high 2 bits go into cl bits 6-7
    shl ah, 6
    or cl, ah

    pop dx
    pop bx
    pop ax
    ret

; --- Data ------------------------------------------------------------
boot_drive:  db 0
retry_count: db 0
lba_start:   dd 1                  ; stage-2 starts at LBA 1

msg_loading: db 'SentinelOS stage1: loading stage-2...', 0x0D, 0x0A, 0
msg_load_fail:
    db 'STAGE1: disk read error, halting.', 0x0D, 0x0A, 0

; --- Pad bootstrap code to 446 bytes (0x1BE) -------------------------
times 446 - ($ - $$) db 0

; --- Partition table (4 entries x 16 bytes) --------------------------
; Entry 0: active (0x80), type 0x07 (NTFS), LBA start 64, size rest of disk.
    db 0x80                      ; bootable/active
    db 0x00, 0x00, 0x00          ; CHS start (unused)
    db 0x07                      ; type = NTFS/HPFS
    db 0x00, 0x00, 0x00          ; CHS end (unused)
    dd 64                        ; LBA start = 64
    dd 0xFFFFFFFF                ; sector count = rest of disk

; Entries 1-3: empty (all zeros)
times 3 * 16 db 0

; --- Boot signature --------------------------------------------------
    db 0x55
    db 0xAA

; --- Pad to exactly 512 bytes ----------------------------------------
times 512 - ($ - $$) db 0
