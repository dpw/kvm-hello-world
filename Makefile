.PHONY: run
run: kvm-hello-world
	./kvm-hello-world
	./kvm-hello-world -s
	./kvm-hello-world -p
	./kvm-hello-world -l

kvm-hello-world: kvm-hello-world.c payload.o
	$(CC) -Wall -Wextra -Werror $^ -o $@

payload.o: payload.ld code16.o code32.o code64.o
	$(LD) -T $< -o $@

.PHONY: clean
clean:
	$(RM) kvm-hello-world code16.o code32.o code64.o payload.o
