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

const unsigned char code16[] = {
	0xb0, 42, /* MOV AL, 42 */
	0xf4, /* HLT */
};

int run_real_mode(struct vm *vm, struct vcpu *vcpu)
{
	struct kvm_sregs sregs;
	struct kvm_regs regs;
	int res;

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

	memcpy(vm->mem, code16, sizeof code16);

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

	if (ioctl(vcpu->fd, KVM_GET_REGS, &regs) < 0) {
		perror("KVM_GET_REGS");
		exit(1);
	}

	res = (regs.rax == 42);
	printf("RAX = %lld: %s\n", regs.rax,
	       res ? "OK" : "wrong");
	return res;
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

const unsigned char code32[] = {
	0xb8, 42, 0, 0, 0, /* MOV EAX, 42 */
	0xf4, /* HLT */
};

int run_prot32_mode(struct vm *vm, struct vcpu *vcpu)
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

	struct kvm_sregs sregs;
	struct kvm_regs regs;
	int res;
	uint64_t *gdt;

        if (ioctl(vcpu->fd, KVM_GET_SREGS, &sregs) < 0) {
		perror("KVM_GET_SREGS");
		exit(1);
	}

	sregs.cr0 |= 1; /* set PE: enter protected mode */
	sregs.gdt.base = 4096;
	sregs.gdt.limit = 3 * 8 - 1;

	gdt = (void *)(vm->mem + sregs.gdt.base);
	/* gdt[0] is the null segment */

	seg.type = 11; /* Code: execute, read, accessed */
	seg.selector = 1;
	fill_segment_descriptor(gdt, &seg);
	sregs.cs = seg;

	seg.type = 3; /* Data: read/write, accessed */
	seg.selector = 2;
	fill_segment_descriptor(gdt, &seg);
	sregs.cs = sregs.ds = sregs.es = sregs.fs = sregs.gs = sregs.ss = seg;

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

	memcpy(vm->mem, code32, sizeof code32);

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

	if (ioctl(vcpu->fd, KVM_GET_REGS, &regs) < 0) {
		perror("KVM_GET_REGS");
		exit(1);
	}

	res = (regs.rax == 42);
	printf("RAX = %lld: %s\n", regs.rax,
	       res ? "OK" : "wrong");
	return res;
}

int main(int argc, char **argv)
{
	struct vm vm;
	struct vcpu vcpu;
	enum { REAL_MODE, PROT32_MODE } mode = REAL_MODE;
	int opt;

	while ((opt = getopt(argc, argv, "rs")) != -1) {
		switch (opt) {
		case 'r':
			mode = REAL_MODE;
			break;

		case 's':
			mode = PROT32_MODE;
			break;

		default:
			fprintf(stderr, "Usage: %s [ -r | -s ]\n", argv[0]);
			return 1;
		}
	}

	vm_init(&vm, 0x100000);
	vcpu_init(&vm, &vcpu);

	switch (mode) {
	case REAL_MODE:
		return !run_real_mode(&vm, &vcpu);

	case PROT32_MODE:
		return !run_prot32_mode(&vm, &vcpu);
	}

	return 1;
}
