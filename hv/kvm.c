#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include "kvm.h"

static struct kvm_device *kvm_dev;

static void *kvm_vm_backend(void) {
  struct kvm *kv;
  kv = malloc(sizeof *kv);
  if (!kv)
    return NULL;
  kv->dev = kvm_dev;
  kv->fd = ioctl(kvm_dev->fd, KVM_CREATE_VM, 0);
  if (kv->fd < 0)
    return NULL;
  return kv;
}

static int kvm_setup_cpuid(int vcpufd, int id) {
  struct {
    struct kvm_cpuid2 hdr;
    struct kvm_cpuid_entry2 entries[128];
  } cpuid;
  cpuid.hdr.nent = 128;
  if (ioctl(kvm_dev->fd, KVM_GET_SUPPORTED_CPUID, &cpuid) < 0)
    return -1;
  for (u32 i = 0; i < cpuid.hdr.nent; i++) {
    struct kvm_cpuid_entry2 *e = &cpuid.entries[i];
    if (e->function == 1) {
      e->ebx = (e->ebx & 0x00ffffff) | ((u32)id << 24);
      fprintf(stderr, "cpuid[%d]: leaf=1 ebx=0x%08x (apicid=%d)\n",
              id, e->ebx, id);
    }
    if (e->function == 0xb || e->function == 0x1f)
      e->edx = id;
  }
  return ioctl(vcpufd, KVM_SET_CPUID2, &cpuid);
}

static void *kvm_vcpu_backend(struct vm *vm, int id) {
  struct kvm *kv = KVM(vm);
  struct kvm_vcpu *kvp;
  kvp = malloc(sizeof *kvp);
  if (!kvp)
    return NULL;
  kvp->fd = ioctl(kv->fd, KVM_CREATE_VCPU, id);
  if (kvp->fd < 0)
    return NULL;
  if (kvm_setup_cpuid(kvp->fd, id) < 0)
    return NULL;
  kvp->run = mmap(NULL, kvm_dev->vcpu_mmap_size, PROT_READ | PROT_WRITE,
                  MAP_SHARED, kvp->fd, 0);
  if (kvp->run == MAP_FAILED)
    return NULL;
  return kvp;
}

static int kvm_mapmem(struct vm *vm, ulong gpa, void *hva, ulong size, int slot) {
  struct kvm *k = KVM(vm);
  struct kvm_userspace_memory_region region = {
    .slot = slot,
    .guest_phys_addr = gpa,
    .memory_size = size,
    .userspace_addr = (u64)hva,
  };
  return ioctl(k->fd, KVM_SET_USER_MEMORY_REGION, &region);
}

static int kvm_run_vcpu(struct vcpu *vcpu, struct vcpu_exit *e) {
  struct kvm_vcpu *kvp = KVM_VCPU(vcpu);
  if (ioctl(kvp->fd, KVM_RUN, 0) < 0) {
    if (errno == EINTR) {
      e->reason = VCPU_EXIT_INTR;
      return 0;
    }
    return -1;
  }
  switch (kvp->run->exit_reason) {
  case KVM_EXIT_HLT:
    e->reason = VCPU_EXIT_HLT;
    break;
  case KVM_EXIT_IO:
    e->reason = VCPU_EXIT_IO;
    e->io.direction = kvp->run->io.direction;
    e->io.port = kvp->run->io.port;
    e->io.size = kvp->run->io.size;
    e->io.count = kvp->run->io.count;
    e->io.data = (char *)kvp->run + kvp->run->io.data_offset;
    break;
  case KVM_EXIT_SHUTDOWN:
    e->reason = VCPU_EXIT_SHUTDOWN;
    break;
  case KVM_EXIT_MMIO:
    e->reason = VCPU_EXIT_MMIO;
    e->mmio.addr = kvp->run->mmio.phys_addr;
    e->mmio.data = kvp->run->mmio.data;
    e->mmio.len = kvp->run->mmio.len;
    e->mmio.is_write = kvp->run->mmio.is_write;
    break;
  case KVM_EXIT_IRQ_WINDOW_OPEN:
    e->reason = VCPU_EXIT_IRQ_WINDOW;
    break;
  case KVM_EXIT_INTERNAL_ERROR:
    e->reason = VCPU_EXIT_INTERNAL_ERROR;
    e->internal.suberror = kvp->run->internal.suberror;
    break;
  case KVM_EXIT_SYSTEM_EVENT:
    e->reason = VCPU_EXIT_SYSTEM_EVENT;
    e->system.type = kvp->run->system_event.type;
    break;
  case KVM_EXIT_DEBUG:
    e->reason = VCPU_EXIT_DEBUG;
    break;
  default:
    e->reason = VCPU_EXIT_UNKNOWN;
    break;
  }
  return 0;
}

static void seg_to_kvm(struct kvm_segment *k, struct vcpu_segment *v) {
  k->base     = v->base;
  k->limit    = v->limit;
  k->selector = v->selector;
  k->type     = v->type;
  k->present  = v->present;
  k->dpl      = v->dpl;
  k->db       = v->db;
  k->s        = v->s;
  k->l        = v->l;
  k->g        = v->g;
}

static void seg_from_kvm(struct vcpu_segment *v, struct kvm_segment *k) {
  v->base     = k->base;
  v->limit    = k->limit;
  v->selector = k->selector;
  v->type     = k->type;
  v->present  = k->present;
  v->dpl      = k->dpl;
  v->db       = k->db;
  v->s        = k->s;
  v->l        = k->l;
  v->g        = k->g;
}

static int kvm_get_regs(struct vcpu *vcpu, struct vcpu_regs *regs) {
  struct kvm_vcpu *kvp = KVM_VCPU(vcpu);
  struct kvm_regs kr;
  if (ioctl(kvp->fd, KVM_GET_REGS, &kr) < 0)
    return -1;
  regs->rax = kr.rax; regs->rbx = kr.rbx;
  regs->rcx = kr.rcx; regs->rdx = kr.rdx;
  regs->rsi = kr.rsi; regs->rdi = kr.rdi;
  regs->rsp = kr.rsp; regs->rbp = kr.rbp;
  regs->r8  = kr.r8;  regs->r9  = kr.r9;
  regs->r10 = kr.r10; regs->r11 = kr.r11;
  regs->r12 = kr.r12; regs->r13 = kr.r13;
  regs->r14 = kr.r14; regs->r15 = kr.r15;
  regs->rip = kr.rip; regs->rflags = kr.rflags;
  return 0;
}

static int kvm_set_regs(struct vcpu *vcpu, struct vcpu_regs *regs) {
  struct kvm_vcpu *kvp = KVM_VCPU(vcpu);
  struct kvm_regs kr;
  memset(&kr, 0, sizeof kr);
  kr.rax = regs->rax; kr.rbx = regs->rbx;
  kr.rcx = regs->rcx; kr.rdx = regs->rdx;
  kr.rsi = regs->rsi; kr.rdi = regs->rdi;
  kr.rsp = regs->rsp; kr.rbp = regs->rbp;
  kr.r8  = regs->r8;  kr.r9  = regs->r9;
  kr.r10 = regs->r10; kr.r11 = regs->r11;
  kr.r12 = regs->r12; kr.r13 = regs->r13;
  kr.r14 = regs->r14; kr.r15 = regs->r15;
  kr.rip = regs->rip; kr.rflags = regs->rflags;
  return ioctl(kvp->fd, KVM_SET_REGS, &kr);
}

static int kvm_get_sregs(struct vcpu *vcpu, struct vcpu_sregs *sregs) {
  struct kvm_vcpu *kvp = KVM_VCPU(vcpu);
  struct kvm_sregs ks;
  if (ioctl(kvp->fd, KVM_GET_SREGS, &ks) < 0)
    return -1;
  seg_from_kvm(&sregs->cs, &ks.cs);
  seg_from_kvm(&sregs->ds, &ks.ds);
  seg_from_kvm(&sregs->es, &ks.es);
  seg_from_kvm(&sregs->fs, &ks.fs);
  seg_from_kvm(&sregs->gs, &ks.gs);
  seg_from_kvm(&sregs->ss, &ks.ss);
  sregs->cr0 = ks.cr0; sregs->cr2 = ks.cr2;
  sregs->cr3 = ks.cr3; sregs->cr4 = ks.cr4;
  return 0;
}

static int kvm_set_sregs(struct vcpu *vcpu, struct vcpu_sregs *sregs) {
  struct kvm_vcpu *kvp = KVM_VCPU(vcpu);
  struct kvm_sregs ks;
  if (ioctl(kvp->fd, KVM_GET_SREGS, &ks) < 0)
    return -1;
  seg_to_kvm(&ks.cs, &sregs->cs);
  seg_to_kvm(&ks.ds, &sregs->ds);
  seg_to_kvm(&ks.es, &sregs->es);
  seg_to_kvm(&ks.fs, &sregs->fs);
  seg_to_kvm(&ks.gs, &sregs->gs);
  seg_to_kvm(&ks.ss, &sregs->ss);
  ks.cr0 = sregs->cr0; ks.cr2 = sregs->cr2;
  ks.cr3 = sregs->cr3; ks.cr4 = sregs->cr4;
  return ioctl(kvp->fd, KVM_SET_SREGS, &ks);
}

static int kvm_set_debug(struct vcpu *vcpu, int singlestep) {
  struct kvm_vcpu *kvp = KVM_VCPU(vcpu);
  struct kvm_guest_debug dbg;
  memset(&dbg, 0, sizeof dbg);
  if (singlestep)
    dbg.control = KVM_GUESTDBG_ENABLE | KVM_GUESTDBG_SINGLESTEP;
  return ioctl(kvp->fd, KVM_SET_GUEST_DEBUG, &dbg);
}

static int kvm_inject_irq(struct vcpu *vcpu, int vector) {
  struct kvm_vcpu *kvp = KVM_VCPU(vcpu);
  if (!kvp->run->ready_for_interrupt_injection) {
    kvp->run->request_interrupt_window = 1;
    return -1;
  }
  kvp->run->request_interrupt_window = 0;
  struct kvm_interrupt irq = { .irq = (u32)vector };
  return ioctl(kvp->fd, KVM_INTERRUPT, &irq);
}

static struct backend kvm = {
  .vm_backend   = kvm_vm_backend,
  .vcpu_backend = kvm_vcpu_backend,
  .mapmem       = kvm_mapmem,
  .vcpu_run     = kvm_run_vcpu,
  .get_regs     = kvm_get_regs,
  .set_regs     = kvm_set_regs,
  .get_sregs    = kvm_get_sregs,
  .set_sregs    = kvm_set_sregs,
  .set_debug    = kvm_set_debug,
  .inject_irq   = kvm_inject_irq,
};

struct backend *kvm_backend(void) {
  int fd;
  fd = open("/dev/kvm", O_RDWR);
  if (fd < 0)
    return NULL;
  if (ioctl(fd, KVM_GET_API_VERSION, 0) != KVM_API_VERSION)
    return NULL;
  kvm_dev = malloc(sizeof *kvm_dev);
  if (!kvm_dev)
    return NULL;
  kvm_dev->fd = fd;
  kvm_dev->vcpu_mmap_size = ioctl(fd, KVM_GET_VCPU_MMAP_SIZE, 0);
  if (kvm_dev->vcpu_mmap_size < 0)
    return NULL;
  return &kvm;
}
