CFLAGS = -Wall -Wextra -Werror

FREESTANDING_FLAGS =\
		-ffreestanding\
		-fno-asynchronous-unwind-tables\
		-nodefaultlibs\
		-nostartfiles\
		-nostdlib\

.PHONY: run
run: kvm-hello-world
	./kvm-hello-world
	./kvm-hello-world -s
	./kvm-hello-world -p
	./kvm-hello-world -l

kvm-hello-world: kvm-hello-world.o payload.o
	$(CC) $^ -o $@

payload.o: payload.ld code16.o code32.o code64.o
	$(LD) -T $< -o $@

code64.o: CFLAGS += $(FREESTANDING_FLAGS)

.PHONY: clean
clean:
	$(RM) kvm-hello-world code16.o code32.o code64.o payload.o
