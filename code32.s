        .code32
        .global code32, code32_end
code32:
        movl $42, %eax
        movl %eax, 0x400
        hlt
code32_end:
