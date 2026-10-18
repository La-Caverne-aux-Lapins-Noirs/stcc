; TOS hands us a basepage pointer at 4(sp), with all free RAM allocated.
        section text
        xdef __stcc_start,__stcc_basepage,_stcc_basepage
        xref _main,__stcc_stack_end,__stcc_program_end
        xref ___stcc_init_args,_stcc_argc,_stcc_argv
__stcc_start:
        move.l  4(sp),a0
        move.l  a0,a2
        move.l  $18(a0),a1
        move.l  $1c(a0),d0
        moveq   #0,d1
.clear:
        tst.l   d0
        beq.w   .cleared
        move.b  d1,(a1)+
        subq.l  #1,d0
        bra.w   .clear
.cleared:
        move.l  a2,__stcc_basepage
        lea     __stcc_stack_end,sp
        move.l  #__stcc_program_end,d0
        sub.l   a2,d0
        move.l  d0,-(sp)
        move.l  a2,-(sp)
        clr.w   -(sp)
        move.w  #$4a,-(sp)
        trap    #1
        lea     12(sp),sp
        jsr     ___stcc_init_args
        tst.w   d0
        beq.w   .arguments_ready
        move.w  #127,d0              ; argument-vector allocation/size failure
        bra.w   .exit
.arguments_ready:
        move.l  _stcc_argv,-(sp)
        move.w  _stcc_argc,-(sp)
        jsr     _main
        lea     6(sp),sp
.exit:
        move.w  d0,-(sp)
        move.w  #$4c,-(sp)
        trap    #1
        illegal
        section bss
__stcc_basepage:
_stcc_basepage:
        ds.l    1
