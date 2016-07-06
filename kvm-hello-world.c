#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <string.h>
#include <stdint.h>
#include <linux/kvm.h>

struct vm {
	int sys_fd;
	int fd;
	char *mem;
};

void vm_init(struct vm *vm, size_t mem_size)
{
	int api_ver;
	struct kvm_userspace_memory_region memreg;

	vm->sys_fd = open("/dev/kvm", O_RDWR);
	if (vm->sys_fd < 0) {
		perror("open /dev/kvm");
		exit(1);
	}

	api_ver = ioctl(vm->sys_fd, KVM_GET_API_VERSION, 0);
	if (api_ver < 0) {
		perror("KVM_GET_API_VERSION");
		exit(1);
	}

	if (api_ver != KVM_API_VERSION) {
		fprintf(stderr, "Got KVM api version %d, expected %d\n",
			api_ver, KVM_API_VERSION);
		exit(1);
	}

	vm->fd = ioctl(vm->sys_fd, KVM_CREATE_VM, 0);
	if (vm->fd < 0) {
		perror("KVM_CREATE_VM");
		exit(1);
	}

        if (ioctl(vm->fd, KVM_SET_TSS_ADDR, 0xfffbd000) < 0) {
                perror("KVM_SET_TSS_ADDR");
		exit(1);
	}

	vm->mem = mmap(NULL, mem_size, PROT_READ | PROT_WRITE,
		   MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
	if (vm->mem == MAP_FAILED) {
		perror("mmap mem");
		exit(1);
	}

	madvise(vm->mem, mem_size, MADV_MERGEABLE);

	memreg.slot = 0;
	memreg.flags = 0;
	memreg.guest_phys_addr = 0;
	memreg.memory_size = mem_size;
	memreg.userspace_addr = (unsigned long)vm->mem;
        if (ioctl(vm->fd, KVM_SET_USER_MEMORY_REGION, &memreg) < 0) {
		perror("KVM_SET_USER_MEMORY_REGION");
                exit(1);
	}
}

struct vcpu {
	int fd;
	struct kvm_run *kvm_run;
};

void vcpu_init(struct vm *vm, struct vcpu *vcpu)
{
	int vcpu_mmap_size;

	vcpu->fd = ioctl(vm->fd, KVM_CREATE_VCPU, 0);
        if (vcpu->fd < 0) {
		perror("KVM_CREATE_VCPU");
                exit(1);
	}

	vcpu_mmap_size = ioctl(vm->sys_fd, KVM_GET_VCPU_MMAP_SIZE, 0);
        if (vcpu_mmap_size <= 0) {
		perror("KVM_GET_VCPU_MMAP_SIZE");
                exit(1);
	}

	vcpu->kvm_run = mmap(NULL, vcpu_mmap_size, PROT_READ | PROT_WRITE,
			     MAP_SHARED, vcpu->fd, 0);
	if (vcpu->kvm_run == MAP_FAILED) {
		perror("mmap kvm_run");
		exit(1);
	}
}

int check(struct vm *vm, struct vcpu *vcpu, size_t sz)
{
	struct kvm_regs regs;
	uint64_t memval = 0;

	if (ioctl(vcpu->fd, KVM_GET_REGS, &regs) < 0) {
		perror("KVM_GET_REGS");
		exit(1);
	}

	if (regs.rax != 42) {
		printf("Wrong result: {E,R,}AX is %lld\n", regs.rax);
		return 0;
	}

	memcpy(&memval, &vm->mem[0x400], sz);
	if (memval != 42) {
		printf("Wrong result: memory at 0x400 is %lld\n",
		       (unsigned long long)memval);
		return 0;
	}

	return 1;
}

extern const unsigned char code16[], code16_end[];

int run_real_mode(struct vm *vm, struct vcpu *vcpu)
{
	struct kvm_sregs sregs;
	struct kvm_regs regs;

	printf("Testing real mode\n");

        if (ioctl(vcpu->fd, KVM_GET_SREGS, &sregs) < 0) {
		perror("KVM_GET_SREGS");
		exit(1);
	}

	sregs.cs.selector = 0;
	sregs.cs.base = 0;

        if (ioctl(vcpu->fd, KVM_SET_SREGS, &sregs) < 0) {
		perror("KVM_SET_SREGS");
		exit(1);
	}

	memset(&regs, 0, sizeof(regs));
	/* Clear all FLAGS bits, except bit 1 which is always set. */
	regs.rflags = 2;
	regs.rip = 0;

	if (ioctl(vcpu->fd, KVM_SET_REGS, &regs) < 0) {
		perror("KVM_SET_REGS");
		exit(1);
	}

	memcpy(vm->mem, code16, code16_end-code16);

	if (ioctl(vcpu->fd, KVM_RUN, 0) < 0) {
		perror("KVM_RUN");
		exit(1);
	}

	if (vcpu->kvm_run->exit_reason != KVM_EXIT_HLT) {
		fprintf(stderr,
			"Got exit_reason %d, expected KVM_EXIT_HLT (%d)\n",
			vcpu->kvm_run->exit_reason, KVM_EXIT_HLT);
		exit(1);
	}

	return check(vm, vcpu, 2);
}

void fill_segment_descriptor(uint64_t *dt, struct kvm_segment *seg)
{
	uint32_t limit = seg->g ? seg->limit >> 12 : seg->limit;
	dt[seg->selector] = (limit & 0xffff) /* Limit bits 0:15 */
		| (seg->base & 0xffffff) << 16 /* Base bits 0:23 */
		| (uint64_t)seg->type << 40
		| (uint64_t)seg->s << 44 /* system or code/data */
		| (uint64_t)seg->dpl << 45 /* Privilege level */
		| (uint64_t)seg->present << 47
		| (limit & 0xf0000ULL) << 48 /* Limit bits 16:19 */
		| (uint64_t)seg->avl << 52 /* Available for system software */
		| (uint64_t)seg->l << 53 /* 64-bit code segment */
		| (uint64_t)seg->db << 54 /* 16/32-bit segment */
		| (uint64_t)seg->g << 55 /* 4KB granularity */
		| (seg->base & 0xff000000ULL) << 56; /* Base bits 24:31 */
}

extern const unsigned char code32[], code32_end[];

static void setup_protected_mode(struct vm *vm, struct kvm_sregs *sregs)
{
	struct kvm_segment seg = {
		.base = 0,
		.limit = 0xffffffff,
		.selector = 1,
		.present = 1,
		.dpl = 0,
		.db = 1,
		.s = 1, /* Code/data */
		.l = 0,
		.g = 1, /* 4KB granularity */
	};
	uint64_t *gdt;

	sregs->cr0 |= 1; /* set PE: enter protected mode */
	sregs->gdt.base = 4096;
	sregs->gdt.limit = 3 * 8 - 1;

	gdt = (void *)(vm->mem + sregs->gdt.base);
	/* gdt[0] is the null segment */

	seg.type = 11; /* Code: execute, read, accessed */
	seg.selector = 1;
	fill_segment_descriptor(gdt, &seg);
	sregs->cs = seg;

	seg.type = 3; /* Data: read/write, accessed */
	seg.selector = 2;
	fill_segment_descriptor(gdt, &seg);
	sregs->cs = sregs->ds = sregs->es = sregs->fs = sregs->gs
		= sregs->ss = seg;
}

int run_protected_mode(struct vm *vm, struct vcpu *vcpu)
{
	struct kvm_sregs sregs;
	struct kvm_regs regs;

	printf("Testing protected mode\n");

        if (ioctl(vcpu->fd, KVM_GET_SREGS, &sregs) < 0) {
		perror("KVM_GET_SREGS");
		exit(1);
	}

	setup_protected_mode(vm, &sregs);

        if (ioctl(vcpu->fd, KVM_SET_SREGS, &sregs) < 0) {
		perror("KVM_SET_SREGS");
		exit(1);
	}

	memset(&regs, 0, sizeof(regs));
	/* Clear all FLAGS bits, except bit 1 which is always set. */
	regs.rflags = 2;
	regs.rip = 0;

	if (ioctl(vcpu->fd, KVM_SET_REGS, &regs) < 0) {
		perror("KVM_SET_REGS");
		exit(1);
	}

	memcpy(vm->mem, code32, code32_end-code32);

	if (ioctl(vcpu->fd, KVM_RUN, 0) < 0) {
		perror("KVM_RUN");
		exit(1);
	}

	if (vcpu->kvm_run->exit_reason != KVM_EXIT_HLT) {
		fprintf(stderr,
			"Got exit_reason %d, expected KVM_EXIT_HLT (%d)\n",
			vcpu->kvm_run->exit_reason, KVM_EXIT_HLT);
		exit(1);
	}

	return check(vm, vcpu, 4);
}

extern const unsigned char code32_paged[], code32_paged_end[];

static void setup_paged_32bit_mode(struct vm *vm, struct kvm_sregs *sregs)
{
	uint32_t pd_addr = 0x2000;
	uint32_t *pd = (void *)(vm->mem + pd_addr);

	/* A single 4MB page to cover the memory region */
	pd[0] = 1 << 0 /* Present */
		| 1 << 1 /* R/W */
		| 1 << 2 /* Allow user-mode accesses */
		| 1 << 7; /* 4MB page */
	/* Other PDEs are left zeroed, meaning not present. */

	sregs->cr3 = pd_addr;
	sregs->cr4 = 1 << 4; /* Page size extensions (4MB pages) */
	sregs->cr0 = 1 /* PE (protection enable) */
		| 1 << 1 /* MP (monitor coprocessor) */
		| 1 << 4 /* ET (extension type) */
		| 1 << 5 /* NE (numeric error) */
		| 1 << 16 /* WP (write protect) */
		| 1 << 18; /* AM (alignment mask) */

	/* We don't set cr0.pg here, because that causes a vm entry
	   failure. It's not clear why. Instead, we set it in the VM
	   code. */
}

int run_paged_32bit_mode(struct vm *vm, struct vcpu *vcpu)
{
	struct kvm_sregs sregs;
	struct kvm_regs regs;

	printf("Testing 32-bit paging\n");

        if (ioctl(vcpu->fd, KVM_GET_SREGS, &sregs) < 0) {
		perror("KVM_GET_SREGS");
		exit(1);
	}

	setup_protected_mode(vm, &sregs);
	setup_paged_32bit_mode(vm, &sregs);

        if (ioctl(vcpu->fd, KVM_SET_SREGS, &sregs) < 0) {
		perror("KVM_SET_SREGS");
		exit(1);
	}

	memset(&regs, 0, sizeof(regs));
	/* Clear all FLAGS bits, except bit 1 which is always set. */
	regs.rflags = 2;
	regs.rip = 0;

	if (ioctl(vcpu->fd, KVM_SET_REGS, &regs) < 0) {
		perror("KVM_SET_REGS");
		exit(1);
	}

	memcpy(vm->mem, code32_paged, code32_paged_end-code32_paged);

	if (ioctl(vcpu->fd, KVM_RUN, 0) < 0) {
		perror("KVM_RUN");
		exit(1);
	}

	if (vcpu->kvm_run->exit_reason != KVM_EXIT_HLT) {
		fprintf(stderr,
			"Got exit_reason %d, expected KVM_EXIT_HLT (%d)\n",
			vcpu->kvm_run->exit_reason, KVM_EXIT_HLT);
		exit(1);
	}

	return check(vm, vcpu, 4);
}

int main(int argc, char **argv)
{
	struct vm vm;
	struct vcpu vcpu;
	enum { REAL_MODE, PROTECTED_MODE, PAGED_32BIT_MODE } mode = REAL_MODE;
	int opt;

	while ((opt = getopt(argc, argv, "rsp")) != -1) {
		switch (opt) {
		case 'r':
			mode = REAL_MODE;
			break;

		case 's':
			mode = PROTECTED_MODE;
			break;

		case 'p':
			mode = PAGED_32BIT_MODE;
			break;

		default:
			fprintf(stderr, "Usage: %s [ -r | -s | -p ]\n",
				argv[0]);
			return 1;
		}
	}

	vm_init(&vm, 0x100000);
	vcpu_init(&vm, &vcpu);

	switch (mode) {
	case REAL_MODE:
		return !run_real_mode(&vm, &vcpu);

	case PROTECTED_MODE:
		return !run_protected_mode(&vm, &vcpu);

	case PAGED_32BIT_MODE:
		return !run_paged_32bit_mode(&vm, &vcpu);
	}

	return 1;
}
