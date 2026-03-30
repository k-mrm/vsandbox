#ifndef _VS_VCPU_H
#define _VS_VCPU_H

#include <pthread.h>
#include "vsandbox.h"

struct vm;

struct vcpu_segment {
  u64 base;
  u32 limit;
  u16 selector;
  u8 type;
  u8 present;
  u8 dpl;
  u8 db;
  u8 s;
  u8 l;
  u8 g;
};

struct vcpu_regs {
  u64 rax, rbx, rcx, rdx;
  u64 rsi, rdi, rsp, rbp;
  u64 r8, r9, r10, r11, r12, r13, r14, r15;
  u64 rip, rflags;
};

struct vcpu_sregs {
  struct vcpu_segment cs, ds, es, fs, gs, ss;
  u64 cr0, cr2, cr3, cr4;
};

#define VCPU_EXIT_HLT            0
#define VCPU_EXIT_IO             1
#define VCPU_EXIT_SHUTDOWN       2
#define VCPU_EXIT_MMIO           3
#define VCPU_EXIT_IRQ_WINDOW     4
#define VCPU_EXIT_INTERNAL_ERROR 5
#define VCPU_EXIT_SYSTEM_EVENT   6
#define VCPU_EXIT_DEBUG          7
#define VCPU_EXIT_INTR           8
#define VCPU_EXIT_UNKNOWN        -1

#define VCPU_EXIT_IO_IN     0
#define VCPU_EXIT_IO_OUT    1

struct vcpu_exit_io {
  u8 direction;
  u16 port;
  u8 size;
  u32 count;
  void *data;
};

struct vcpu_exit_mmio {
  u64 addr;
  u8 *data;
  u32 len;
  u8 is_write;
};

struct vcpu_exit_internal {
  u32 suberror;
};

struct vcpu_exit_system {
  u32 type;
};

struct vcpu_exit {
  int reason;
  union {
    struct vcpu_exit_io io;
    struct vcpu_exit_mmio mmio;
    struct vcpu_exit_internal internal;
    struct vcpu_exit_system system;
  };
};

struct vcpu {
  struct vm *vm;
  int id;
  int online;
  int exit;
  volatile int dump;
  pthread_t thread;
  void *priv;
};

extern __thread struct vcpu *current;

struct vcpu *create_vcpu(struct vm *vm, int id);
int vcpu_kick(struct vcpu *vcpu);
void vcpu_wait(struct vcpu *vcpu);
int vcpu_setup_boot(struct vcpu *vcpu, ulong entry, ulong sp);
int vcpu_setup_bios(struct vcpu *vcpu);
int vcpu_set_singlestep(struct vcpu *vcpu, int enable);

#endif  // _VS_VCPU_H
