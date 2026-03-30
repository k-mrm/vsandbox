#ifndef _VS_BACKEND_H
#define _VS_BACKEND_H

#include "vsandbox.h"
#include "vcpu.h"
#include "vm.h"

struct backend {
  void *(*vm_backend)(void);
  void *(*vcpu_backend)(struct vm *vm, int id);
  int (*mapmem)(struct vm *vm, ulong gpa, void *hva, ulong size, int slot);
  int (*vcpu_run)(struct vcpu *vcpu, struct vcpu_exit *exit);
  int (*get_regs)(struct vcpu *vcpu, struct vcpu_regs *regs);
  int (*set_regs)(struct vcpu *vcpu, struct vcpu_regs *regs);
  int (*get_sregs)(struct vcpu *vcpu, struct vcpu_sregs *sregs);
  int (*set_sregs)(struct vcpu *vcpu, struct vcpu_sregs *sregs);
  int (*set_debug)(struct vcpu *vcpu, int singlestep);
  int (*inject_irq)(struct vcpu *vcpu, int vector);
};

extern struct backend *B;
struct backend *kvm_backend(void);

#endif  // _VS_BACKEND_H
