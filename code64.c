#include <stddef.h>
#include <stdint.h>

void
__attribute__((noreturn))
__attribute__((section(".start")))
start(void) {
	*(uint64_t *) 0x400 = 42;

	for (;;)
		asm("hlt" : /* empty */ : "a" (42) : "memory");
}
