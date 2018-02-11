SECTIONS
{
        .payload16 0 : {
                 code16.o(.text)
        }
        .payload32 0 : AT(LOADADDR(.payload16)+SIZEOF(.payload16)) {
                 code32.o(.text)
        }
        .payload64 0 : AT(LOADADDR(.payload32)+SIZEOF(.payload32)) {
                 code64 = .;
                 code64.o(.start)
                 code64.o(.text*)
                 code64.o(.rodata*)
                 code64.o(.data*)
                 code64_end = .;
        }
}
