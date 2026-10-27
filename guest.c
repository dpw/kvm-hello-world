#include <stddef.h>
#include <stdint.h>

/* Obfuscate an address so that gcc doesn't make assumptions about it.
 * This is needed to avoid warnings from gcc about array bounds when
 * accessing memory directly. It is modelled on the linux kernel
 * absolute_pointer / HIDE_RELOC macros. */
#define absolute_pointer(ptr)						\
({									\
	void *__ptr;							\
        __asm__ ("" : "=r"(__ptr) : "0"(ptr));                          \
	__ptr;								\
})

static void outb(uint16_t port, uint8_t value) {
	asm("outb %0,%1" : /* empty */ : "a" (value), "Nd" (port) : "memory");
}

void
__attribute__((noreturn))
__attribute__((section(".start")))
_start(void) {
	const char *p;

	for (p = "Hello, world!\n"; *p; ++p)
		outb(0xE9, *p);

	*(long *)absolute_pointer(0x400) = 42;

	for (;;)
		asm("hlt" : /* empty */ : "a" (42) : "memory");
}
