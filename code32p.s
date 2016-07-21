        .code32
        .global code32_paged, code32_paged_end
code32_paged:
        // Set cr0.pg
        movl %cr0, %eax
        orl $0x80000000, %eax
        movl %eax, %cr0
        movl $42, %eax
        movl %eax, 0x400
        hlt
code32_paged_end:
