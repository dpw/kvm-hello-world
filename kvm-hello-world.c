#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <string.h>
#include <linux/kvm.h>

const unsigned char code[] = {
	0xb0, 42, /* MOV AL, 42 */
	0xf4, /* HLT */
};

int main(void)
{
	size_t mem_size = 0x100000;
	int api_ver, vm_fd, vcpu_fd, vcpu_mmap_size;
	void *mem;
	struct kvm_userspace_memory_region memreg;
	struct kvm_run *kvm_run;
	struct kvm_sregs sregs;
	struct kvm_regs regs;

	int sys_fd = open("/dev/kvm", O_RDWR);
	if (sys_fd < 0) {
		perror("open /dev/kvm");
		return 1;
	}

	api_ver = ioctl(sys_fd, KVM_GET_API_VERSION, 0);
	if (api_ver < 0) {
		perror("KVM_GET_API_VERSION");
		return 1;
	}

	if (api_ver != KVM_API_VERSION) {
		fprintf(stderr, "Got KVM api version %d, expected %d\n",
			api_ver, KVM_API_VERSION);
		return 1;
	}

	vm_fd = ioctl(sys_fd, KVM_CREATE_VM, 0);
	if (vm_fd < 0) {
		perror("KVM_CREATE_VM");
		return 1;
	}

        if (ioctl(vm_fd, KVM_SET_TSS_ADDR, 0xfffbd000) < 0) {
                perror("KVM_SET_TSS_ADDR");
		return 1;
	}

	mem = mmap(NULL, mem_size, PROT_READ | PROT_WRITE,
		   MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
	if (mem == MAP_FAILED) {
		perror("mmap mem");
		return 1;
	}

	madvise(mem, mem_size, MADV_MERGEABLE);

	memreg.slot = 0;
	memreg.flags = 0;
	memreg.guest_phys_addr = 0;
	memreg.memory_size = mem_size;
	memreg.userspace_addr = (unsigned long)mem;
        if (ioctl(vm_fd, KVM_SET_USER_MEMORY_REGION, &memreg) < 0) {
		perror("KVM_SET_USER_MEMORY_REGION");
                return 1;
	}

	vcpu_fd = ioctl(vm_fd, KVM_CREATE_VCPU, 0);
        if (vcpu_fd < 0) {
		perror("KVM_CREATE_VCPU");
                return 1;
	}

	vcpu_mmap_size = ioctl(sys_fd, KVM_GET_VCPU_MMAP_SIZE, 0);
        if (vcpu_mmap_size <= 0) {
		perror("KVM_GET_VCPU_MMAP_SIZE");
                return 1;
	}

	kvm_run = mmap(NULL, vcpu_mmap_size, PROT_READ | PROT_WRITE, MAP_SHARED,
		       vcpu_fd, 0);
	if (kvm_run == MAP_FAILED) {
		perror("mmap kvm_run");
		return 1;
	}

        if (ioctl(vcpu_fd, KVM_GET_SREGS, &sregs) < 0) {
		perror("KVM_GET_SREGS");
		return 1;
	}

	sregs.cs.selector = 0;
	sregs.cs.base = 0;

        if (ioctl(vcpu_fd, KVM_SET_SREGS, &sregs) < 0) {
		perror("KVM_SET_SREGS");
		return 1;
	}

	memset(&regs, 0, sizeof(regs));
	/* Clear all FLAGS bits, except bit 1 which is always set. */
	regs.rflags = 2;
	regs.rip = 0;

	if (ioctl(vcpu_fd, KVM_SET_REGS, &regs) < 0) {
		perror("KVM_SET_REGS");
		return 1;
	}

	memcpy(mem, code, sizeof code);

	if (ioctl(vcpu_fd, KVM_RUN, 0) < 0) {
		perror("KVM_RUN");
		return 1;
	}

	if (kvm_run->exit_reason != KVM_EXIT_HLT) {
		fprintf(stderr,
			"Got exit_reason %d, expected KVM_EXIT_HLT (%d)\n",
			kvm_run->exit_reason, KVM_EXIT_HLT);
		return 1;
	}

	if (ioctl(vcpu_fd, KVM_GET_REGS, &regs) < 0) {
		perror("KVM_GET_REGS");
		return 1;
	}

	printf("RAX = %lld: %s\n", regs.rax,
	       regs.rax == 42 ? "OK" : "wrong");
	return 0;
}
