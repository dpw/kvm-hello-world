.PHONY: run
run: kvm-hello-world
	./kvm-hello-world
	./kvm-hello-world -s

kvm-hello-world: kvm-hello-world.c code16.o code32.o
	$(CC) -Wall -Wextra -Werror $^ -o $@

.PHONY: clean
clean:
	rm kvm-hello-world
