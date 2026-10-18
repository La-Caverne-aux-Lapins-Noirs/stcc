; Default stack for manual --nostartfiles links. Automatic startup also accepts
; --stack-size or --stack-top; arithmetic helpers do not reserve a stack.
        section bss
        xdef __stcc_stack,__stcc_stack_end
__stcc_stack:
        ds.b 16384
__stcc_stack_end:
