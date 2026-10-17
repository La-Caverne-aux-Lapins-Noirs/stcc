; Autonomous ROM startup for an original ST, with at least 512 KiB in bank 0.
; Uses the first 512 KiB only. $78000..$7fcff is reserved for a 32000-byte screen.
; No TOS, BIOS, GEMDOS, cartridge, disk access, RAM detection or keyboard driver.
        section text
        xdef __stcc_rom_start,__stcc_rom_reset,__stcc_rom_halt,__stcc_rom_fault
        xref _main,__stcc_stack_end
        xref __stcc_data_load,__stcc_data_start,__stcc_data_size
        xref __stcc_bss_start,__stcc_bss_end,__stcc_bss_size
__stcc_rom_start:
        ; The ST maps the first eight ROM bytes over RAM for the reset vectors.
        ; The initial SSP overlaps the branch/version; replace it before using SP.
        bra.s   __stcc_rom_reset
        dc.w    $0100
        dc.l    __stcc_rom_reset,__stcc_rom_start,__stcc_bss_end,0,0
        dc.l    $10172026
        dc.w    1,$5d51
        dc.l    0,0,0
        dc.l    $53544343             ; "STCC", deliberately not "ETOS"
__stcc_rom_reset:
        move.w  #$2700,sr
        reset
        move.b  #4,$ffff8001          ; ST MMU: bank 0 = 512 KiB
        lea     __stcc_stack_end,sp
        ; The ST rejects writes to addresses 0..7. Runtime vectors start at 8.
        lea     $00000008,a0
        move.l  #__stcc_rom_fault,d0
        move.w  #253,d1
.vectors:
        move.l  d0,(a0)+
        dbra    d1,.vectors           ; vectors 2..255, 68000 has no VBR

        lea     __stcc_data_load,a0
        lea     __stcc_data_start,a1
        move.l  #__stcc_data_size,d0
        bra.w   .copy_test
.copy:
        move.b  (a0)+,(a1)+
        subq.l  #1,d0
.copy_test:
        tst.l   d0
        bne.w   .copy

        lea     __stcc_bss_start,a0
        move.l  #__stcc_bss_size,d0
        bra.w   .clear_test
.clear:
        clr.b   (a0)+
        subq.l  #1,d0
.clear_test:
        tst.l   d0
        bne.w   .clear
        ; The stack has not been used yet, so clearing its BSS is safe.
        move.w  #$2700,sr
        jsr     _main                ; supervisor mode, hardware IRQs masked
__stcc_rom_halt:
        stop    #$2700               ; main's return value remains in D0
        bra.w   __stcc_rom_halt
__stcc_rom_fault:
        move.w  #$2700,sr
.halt:
        stop    #$2700               ; no assumption about exception frame/SP
        bra.w   .halt
