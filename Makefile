CFLAGS = -Wall -Wextra -Werror -O2

.PHONY: run
run: kvm-hello-world
	./kvm-hello-world
	./kvm-hello-world -s
	./kvm-hello-world -p
	./kvm-hello-world -l

kvm-hello-world: kvm-hello-world.o payload.o
	$(CC) $^ -o $@

payload.o: payload.ld guest16.o guest32.img.o guest64.img.o
	$(LD) -T $< -o $@

guest64.o: guest.c
	$(CC) $(CFLAGS) -m64 -ffreestanding -fno-pic -c -o $@ $^

guest64.img: guest64.o
	$(LD) -T guest.ld $^ -o $@

guest32.o: guest.c
	$(CC) $(CFLAGS) -m32 -ffreestanding -fno-pic -c -o $@ $^

guest32.img: guest32.o
	$(LD) -T guest.ld -m elf_i386 $^ -o $@

%.img.o: %.img
	$(LD) -b binary -r $^ -o $@

.PHONY: clean
clean:
	$(RM) kvm-hello-world kvm-hello-world.o payload.o guest16.o \
		guest32.o guest32.img guest32.img.o \
		guest64.o guest64.img guest64.img.o
