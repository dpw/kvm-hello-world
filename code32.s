        .code32
        .global code32, code32_end
code32:
        movl $42, %eax
        hlt
code32_end:

        .global code32_paged, code32_paged_end
code32_paged:
        movl %cr0, %eax
        orl $0x80000000, %eax
        movl %eax, %cr0

        movl $42, %eax
        movl %eax, 0x500
        hlt
code32_paged_end:
