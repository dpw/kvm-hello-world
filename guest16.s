	.code16
	.text

        # This does the same as the code in guest.c, but in assembly to
        # avoid a dependency on a 16-bit C compiler.

	cld
	mov $0xe9, %dx		# debug output port
	mov $message, %si
1:	lodsb			# %al = *%si++
	test %al, %al
	jz 2f
	out %al, %dx
	jmp 1b

2:	mov $42, %ax
	mov %ax, 0x400
	hlt

message:
	.asciz "Hello world!\n"
