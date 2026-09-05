; P2000T SLOT1 bank identification ROM, assembled with z80asm.
; Normal monitor hand-off at 1010; monitor NMI vector jumps to 1016.
; Text records (36 bytes) are filled by build.py independently in each bank.
    org 0x1000
    db 0x5c,0,0,0,0         ; valid normal cartridge, skip ROM checksum/second header
    db "BANK00 TEST"       ; 11-byte name, replaced per bank
    jp start                ; 1010: normal entry
    jp start                ; 1013: spare entry
    jp start                ; 1016: NMI/soft-reset entry
start:
    di
    ld sp,0x6200            ; standard base RAM; discard old/NMI stack
    xor a
    out (0x10),a            ; disable keyboard interrupt generation
    ld hl,0x5000
    ld de,0x5001
    ld bc,0x07ff
    ld (hl),0
    ldir                    ; clear 2 KiB video RAM

    ld hl,0x1200            ; title
    ld de,0x50a2            ; row 2, column 2 (80-byte row stride)
    ld bc,36
    ldir
    ld hl,0x1240            ; large bank number (double-height teletext)
    ld de,0x5192            ; row 5
    ld bc,36
    ldir
    ld hl,0x1280            ; physical address-bit setting
    ld de,0x52d2            ; row 9
    ld bc,36
    ldir
    ld hl,0x1300            ; label in first 4 KiB ROM quarter
    ld de,0x53c2            ; row 12
    ld bc,36
    ldir
    ld hl,0x2000            ; label in second quarter
    ld de,0x5412            ; row 13
    ld bc,36
    ldir
    ld hl,0x3000            ; label in third quarter
    ld de,0x5462            ; row 14
    ld bc,36
    ldir
    ld hl,0x4000            ; label in fourth quarter
    ld de,0x54b2            ; row 15
    ld bc,36
    ldir
    ld hl,0x1340            ; instructions
    ld de,0x55a2            ; row 18
    ld bc,36
    ldir
idle:
    halt                    ; screen stays visible; NMI restarts the selected bank
    jr idle
