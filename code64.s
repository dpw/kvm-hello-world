        .global code64, code64_end
        .code64
code64:
        movabsq $42, %rax
        movq %rax, 0x400
        hlt
code64_end:
