.PHONY: run
run: kvm-hello-world
	./kvm-hello-world

kvm-hello-world: kvm-hello-world.c
	$(CC) -Wall -Wextra -Werror $^ -o $@

.PHONY: clean
clean:
	rm kvm-hello-world
