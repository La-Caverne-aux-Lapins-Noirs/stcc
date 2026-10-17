; Internal register ABI: d0 = left, d1 = right; d0 = result, d1 = remainder.
; Clobbers d0,d1,a0,a1,CCR. Preserves all other registers.
        section text
        xdef __stcc_mul32,__stcc_udiv32,__stcc_sdiv32
__stcc_mul32:
        movem.l d2-d4,-(sp)
        move.l  d0,d2
        move.l  d1,d3
        moveq   #0,d0
        moveq   #31,d4
.loop:
        lsr.l   #1,d3
        bcc.w   .skip
        add.l   d2,d0
.skip:
        lsl.l   #1,d2
        dbra    d4,.loop
        movem.l (sp)+,d2-d4
        rts
__stcc_udiv32:
        movem.l d2-d4,-(sp)
        move.l  d1,d3
        bne.w   .nonzero
        ; Match hardware integer divide-by-zero exception, vector 5.
        divu.w  #0,d0
.nonzero:
        moveq   #0,d2
        moveq   #31,d4
.loop:
        add.l   d0,d0
        addx.l  d2,d2
        bcs.w   .subtract
        cmp.l   d3,d2
        bcs.w   .next
.subtract:
        sub.l   d3,d2
        addq.l  #1,d0
.next:
        dbra    d4,.loop
        move.l  d2,d1
        movem.l (sp)+,d2-d4
        rts
__stcc_sdiv32:
        movem.l d2-d3,-(sp)
        move.l  d0,d2
        move.l  d0,d3
        eor.l   d1,d3
        tst.l   d0
        bpl.w   .leftpos
        neg.l   d0
.leftpos:
        tst.l   d1
        bpl.w   .rightpos
        neg.l   d1
.rightpos:
        jsr     __stcc_udiv32
        tst.l   d3
        bpl.w   .quotpos
        neg.l   d0
.quotpos:
        tst.l   d2
        bpl.w   .rempos
        neg.l   d1
.rempos:
        movem.l (sp)+,d2-d3
        rts

; Linked after every user object: the stack is above all program BSS.
        section bss
        xdef __stcc_stack_end
__stcc_stack:
        ds.b 16384
__stcc_stack_end:
