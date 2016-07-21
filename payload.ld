SECTIONS
{
        .payload16 0 : {
                 code16.o(.text)
        }
        .payload32 0 : AT(LOADADDR(.payload16)+SIZEOF(.payload16)) {
                 code32.o(.text)
        }
        .payload32p 0 : AT(LOADADDR(.payload32)+SIZEOF(.payload32)) {
                 code32p.o(.text)
        }
        .payload64 0 : AT(LOADADDR(.payload32p)+SIZEOF(.payload32p)) {
                 code64.o(.text)
        }
}
