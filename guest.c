#include <stddef.h>
#include <stdint.h>

static void outb(uint16_t port, uint8_t value) {
	asm("outb %0,%1" :: "a" (value), "Nd" (port) : "memory");
}

__attribute__((noreturn))
__attribute__((section(".start")))
void _start(void) {
	const char *p;
	for (p = "hello vsandbox\n"; *p; ++p)
		outb(0x3f8, *p);
	for (;;)
		asm("hlt" ::: "memory");
}
