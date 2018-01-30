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
