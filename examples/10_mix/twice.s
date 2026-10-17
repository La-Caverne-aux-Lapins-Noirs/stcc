        section text
        xdef _twice
_twice:
        move.w  4(sp),d0
        add.w   d0,d0
        rts
