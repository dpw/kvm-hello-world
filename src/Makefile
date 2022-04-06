CFLAGS = -Wall -Wextra -Werror -O2 -g

.PHONY: run
run: host
	./host

host: host.o payload.o
	$(CC) $^ -o $@

payload.o: payload.ld guest64.img.o
	$(LD) -r -T $< -o $@

guest64.o: guest.c
	$(CC) $(CFLAGS) -I/home/wangzc/Desktop/experiment/linux-source-5.13.0-kvm/include/ -m64 -ffreestanding -fno-pic -c -o $@ $^

guest64.img: guest64.o
	$(LD) -T guest.ld $^ -o $@

%.img.o: %.img
	$(LD) -b binary -r $^ -o $@

.PHONY: clean
clean:
	$(RM) host host.o payload.o \
		guest64.o guest64.img guest64.img.o
