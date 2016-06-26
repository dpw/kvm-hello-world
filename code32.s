        .global code32, code32_end
code32:
        movl $42, %eax
        hlt
code32_end:
