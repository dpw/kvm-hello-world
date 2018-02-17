        .global code64, code64_end
        .code32
code64:
        // We are now in ia32e compatibility mode. Switch to 64-bit
	// code segment
        ljmp $(3 << 3), $1f
        .code64
1:
        movabsq $42, %rax
        movq %rax, 0x400
        hlt
code64_end:
