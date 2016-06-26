        .code16
        .global code16, code16_end
code16:
        movw $42, %ax
        hlt
code16_end:
