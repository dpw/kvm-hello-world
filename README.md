# 工作目标

1. 理解kvm和ept结构
2. 测试hypercall
3. 测试vmfunc



# custom hypercall 

[Implementing a custom hypercall in kvm](https://stackoverflow.com/questions/33590843/implementing-a-custom-hypercall-in-kvm)


# helper ebpfs

```
sudo bpftrace -e 'kprobe:vmx_vcpu_load_vmcs {printf("%s %016lx %d %016lx\n", comm, *arg0, arg1, *arg2);}'
sudo bpftrace -e 'kprobe:vmx_load_mmu_pgd {printf("%s %016lx %016lx %d\n", comm, arg0, arg1, arg2);}'

sudo bpftrace -e 'kfunc:vmx_load_mmu_pgd {printf("%s %016lx %ld %d\n", comm, args->vcpu, args->root_hpa, args->root_level); @[kstack()] = count(); @root_hpa[args->root_hpa] = count(); }'


sudo bpftrace -e 'kfunc:vmx_vcpu_load_vmcs {printf("%s %016lx %d %016lx\n", comm, args->vcpu, args->cpu, args->buddy); @[kstack()] = count(); @vmcs_pos[args->buddy] = count();}'
```





# A minimal KVM example

kvm-hello-world is a very simple example program to demonstrate the
use of the KVM API provided by the Linux kernel.  It acts as a very
simple VM host, and runs a trivial program in a VM.  I tested it on
Intel processors with the VMX hardware virtualization extensions.  It
*might* work on AMD processors with AMD-V, but that hasn't been
tested.

## Background

[KVM](https://en.wikipedia.org/wiki/Kernel-based_Virtual_Machine) is
the Linux kernel subsystem that provides access to hardware
virtualization features of the processor.  On x86, this means Intel's
VMX or AMD's AMD-V.  VMX is also known as
[VT-x](https://en.wikipedia.org/wiki/VT-x); VT-x seems to be the
marketing term, whereas VMX is used in the Intel x86 manual set.

In practice, KVM is m often employed via
[qemu](http://wiki.qemu.org/).  In that case, KVM provides
virtualization of the CPU and a few other key hardware components
intimately associated with the CPU, such as the interrupt controller.
qemu emulates all the devices making up the rest of a typical x86
system.  qemu predates KVM, and can also operate independently of it,
performing CPU virtualization in software instead.

But if you want to learn about the details of KVM, qemu is not a great
resource.  It's a big project with a lot of features and support for
emulating many devices.

There's another project that is much more approachable:
[kvmtool](https://github.com/kvmtool/kvmtool). Like qemu, kvmtool does
full-system emulation.  unlike qemu, it is deliberately minimal,
emulating just a few devices.  But while kvmtool is impressive
demonstration of how simple and clean a KVM-based full-system emulator
can be, it's still far more than a bare-bones example.

So, as no such example seems to exist, I wrote one by studying api.txt
and the kvmtool sources.  (Update: When I wrote this, I had overlooked
https://github.com/soulxu/kvmsample).

## Notes

The code is straightforward.  It:

* Opens `/dev/kvm` and checks the version.
* Makes a `KVM_CREATE_VM` call to creates a VM.
* Uses mmap to allocate some memory for the VM.
* Makes a `KVM_CREATE_VCPU` call to creates a VCPU within the VM, and
  mmaps its control area.
* Sets the FLAGS and CS:IP registers of the VCPU.
* Copies a few bytes of code into the VM memory.
* Makes a `KVM_RUN` call to execute the VCPU.
* Checks that the VCPU execution had the expected result.

A couple of aspects are worth noting:

Note that the Intel VMX extensions did not initially implement support
for real mode.  In fact, they restricted VMX guests to paged protected
mode.  VM hosts were expected to emulate the unsupported modes in
software, only employing VMX when a guest had entered paged protected
mode (KVM does not implement such emulation support; I assume it is
delegated to qemu).  Later VMX implementations (since Westmere aka
Nehalem-C in 2010) include *Unrestricted Guest Mode*: support for
virtualization of all x86 modes in hardware.

The code run in the VM code exits with a HLT instruction.  There are
many ways to cause a VM exit, so why use a HLT instruction?  The most
obvious way might be the VMCALL (or VMMCALL on AMD) instruction, which
it specifically intended to call out to the hypervisor.  But it turns
out the KVM reserves VMCALL/VMMCALL for its internal hypercall
mechanism, without notifying the userspace VM host program of the VM
exits caused by these instructions.  So we need some other way to
trigger a VM exit.  HLT is convenient because it is a single-byte
instruction.


In single file 

```
/* Sample code for /dev/kvm API
 *
 * Copyright (c) 2015 Intel Corporation
 * Author: Josh Triplett <josh@joshtriplett.org>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */
#include <err.h>
#include <fcntl.h>
#include <linux/kvm.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

int main(void)
{
    int kvm, vmfd, vcpufd, ret;
    const uint8_t code[] = {
        0xba, 0xf8, 0x03, /* mov $0x3f8, %dx */
        0x00, 0xd8,       /* add %bl, %al */
        0x04, '0',        /* add $'0', %al */
        0xee,             /* out %al, (%dx) */
        0xb0, '\n',       /* mov $'\n', %al */
        0xee,             /* out %al, (%dx) */
        0xf4,             /* hlt */
    };
    uint8_t *mem;
    struct kvm_sregs sregs;
    size_t mmap_size;
    struct kvm_run *run;

    kvm = open("/dev/kvm", O_RDWR | O_CLOEXEC);
    if (kvm == -1)
        err(1, "/dev/kvm");

    /* Make sure we have the stable version of the API */
    ret = ioctl(kvm, KVM_GET_API_VERSION, NULL);
    if (ret == -1)
        err(1, "KVM_GET_API_VERSION");
    if (ret != 12)
        errx(1, "KVM_GET_API_VERSION %d, expected 12", ret);

    vmfd = ioctl(kvm, KVM_CREATE_VM, (unsigned long)0);
    if (vmfd == -1)
        err(1, "KVM_CREATE_VM");

    /* Allocate one aligned page of guest memory to hold the code. */
    mem = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (!mem)
        err(1, "allocating guest memory");
    memcpy(mem, code, sizeof(code));

    /* Map it to the second page frame (to avoid the real-mode IDT at 0). */
    struct kvm_userspace_memory_region region = {
        .slot = 0,
        .guest_phys_addr = 0x1000,
        .memory_size = 0x1000,
        .userspace_addr = (uint64_t)mem,
    };
    ret = ioctl(vmfd, KVM_SET_USER_MEMORY_REGION, &region);
    if (ret == -1)
        err(1, "KVM_SET_USER_MEMORY_REGION");

    vcpufd = ioctl(vmfd, KVM_CREATE_VCPU, (unsigned long)0);
    if (vcpufd == -1)
        err(1, "KVM_CREATE_VCPU");

    /* Map the shared kvm_run structure and following data. */
    ret = ioctl(kvm, KVM_GET_VCPU_MMAP_SIZE, NULL);
    if (ret == -1)
        err(1, "KVM_GET_VCPU_MMAP_SIZE");
    mmap_size = ret;
    if (mmap_size < sizeof(*run))
        errx(1, "KVM_GET_VCPU_MMAP_SIZE unexpectedly small");
    run = mmap(NULL, mmap_size, PROT_READ | PROT_WRITE, MAP_SHARED, vcpufd, 0);
    if (!run)
        err(1, "mmap vcpu");

    /* Initialize CS to point at 0, via a read-modify-write of sregs. */
    ret = ioctl(vcpufd, KVM_GET_SREGS, &sregs);
    if (ret == -1)
        err(1, "KVM_GET_SREGS");
    sregs.cs.base = 0;
    sregs.cs.selector = 0;
    ret = ioctl(vcpufd, KVM_SET_SREGS, &sregs);
    if (ret == -1)
        err(1, "KVM_SET_SREGS");

    /* Initialize registers: instruction pointer for our code, addends, and
     * initial flags required by x86 architecture. */
    struct kvm_regs regs = {
        .rip = 0x1000,
        .rax = 2,
        .rbx = 2,
        .rflags = 0x2,
    };
    ret = ioctl(vcpufd, KVM_SET_REGS, &regs);
    if (ret == -1)
        err(1, "KVM_SET_REGS");

    /* Repeatedly run code and handle VM exits. */
    while (1) {
        ret = ioctl(vcpufd, KVM_RUN, NULL);
        if (ret == -1)
            err(1, "KVM_RUN");
        switch (run->exit_reason) {
        case KVM_EXIT_HLT:
            puts("KVM_EXIT_HLT");
            return 0;
        case KVM_EXIT_IO:
            if (run->io.direction == KVM_EXIT_IO_OUT && run->io.size == 1 && run->io.port == 0x3f8 && run->io.count == 1)
                putchar(*(((char *)run) + run->io.data_offset));
            else
                errx(1, "unhandled KVM_EXIT_IO");
            break;
        case KVM_EXIT_FAIL_ENTRY:
            errx(1, "KVM_EXIT_FAIL_ENTRY: hardware_entry_failure_reason = 0x%llx",
                 (unsigned long long)run->fail_entry.hardware_entry_failure_reason);
        case KVM_EXIT_INTERNAL_ERROR:
            errx(1, "KVM_EXIT_INTERNAL_ERROR: suberror = 0x%x", run->internal.suberror);
        default:
            errx(1, "exit_reason = 0x%x", run->exit_reason);
        }
    }
}
```