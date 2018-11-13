;--------------------------------SETTINGS------------------------------------------
DEF_P32_BSECTOR   EQU        1

DEF_P32_SEGM      EQU        2000h    ; segment to read
DEF_P32_OFFSET    EQU        0000h   ; offset to read
DEF_N             EQU        4        ; number of sectors to read

;----------------------------------------------------------------------------------

use16
org 7C00h
boot:
    cli
    cld
    xor    ax, ax
    mov    ss, ax
    mov    ds, ax
    mov    sp, 7C00h

    ; enable A20 gate
    in    al, 92h
    test    al, 02h
    jnz    .no92
    or    al, 02h
    out    92h, al

.no92:
    ; disable PIC
    mov    al, 0FFh
    out    0A1h, al
    out    21h, al

load_p32:

    push    0000            ; hight  of start sector
    push    0000            ; hight  of start sector
    push    0000            ; low addr of start sector
    push    DEF_P32_BSECTOR ; low addr of start sector                 
    push    DEF_P32_SEGM    ; segment
    push    DEF_P32_OFFSET  ; offset
    push    DEF_N           ; number sectors to read
    push    0010h           ; packet size
    mov    si, sp
    mov    ah, 42h
    int    13h
    add    sp, 10h
    jmp    0x2000:0x0000

    hlt
    jmp    $





rb boot+512-2-$
dw 0AA55h