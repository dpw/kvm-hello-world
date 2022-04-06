SECTIONS
{
        .payload16 0 : {
                guest16 = .;
                guest16.o(.text)
                guest16_end = .;
        }
        .payload32 0 : AT(LOADADDR(.payload16)+SIZEOF(.payload16)) {
                guest32 = .;
                guest32.img.o
                guest32_end = .;
        }
        .payload64 0 : AT(LOADADDR(.payload32)+SIZEOF(.payload32)) {
                guest64 = .;
                guest64.img.o
                guest64_end = .;
        }
}
