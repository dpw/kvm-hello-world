#include <stddef.h>
#include <stdint.h>

#define KVM_HYPERCALL "vmcall"
#define KVM_HC_HELLO_HYPERCALL 12

static void outb(uint16_t port, uint8_t value) {
	asm("outb %0,%1" : /* empty */ : "a" (value), "Nd" (port) : "memory");
}



static inline long kvm_hypercall0(unsigned int nr)
{
	long ret;
	asm volatile(KVM_HYPERCALL
		     : "=a"(ret)
		     : "a"(nr)
		     : "memory");
	return ret;
}


void
__attribute__((noreturn))
__attribute__((section(".start")))
_start(void) {
	const char *p;

	for (p = "Hello, world!\n"; *p; ++p)
		outb(0xE9, *p);

	*(long *) 0x400 = 42;

    kvm_hypercall0(KVM_HC_HELLO_HYPERCALL);

	for (;;)
		asm("hlt" : /* empty */ : "a" (42) : "memory");
}
