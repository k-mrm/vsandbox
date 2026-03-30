#ifndef _VS_KVM_H
#define _VS_KVM_H

#include "vsandbox.h"
#include "vcpu.h"
#include "vm.h"
#include "backend.h"
#include <linux/kvm.h>

struct kvm_device {
  int fd;
  int vcpu_mmap_size;
};

struct kvm {
  struct kvm_device *dev;
  int fd;
};

struct kvm_vcpu {
  int fd;
  struct kvm_run *run;
};

#define KVM(_v)      ((struct kvm *)((_v)->priv))
#define KVM_VCPU(_v) ((struct kvm_vcpu *)((_v)->priv))

#endif  // _VS_KVM_H
