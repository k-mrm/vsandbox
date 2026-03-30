#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include "vsandbox.h"
#include "vcpu.h"
#include "vm.h"
#include "backend.h"

__thread struct vcpu *current;

static void *guest_phys(struct vm *vm, ulong gpa, size_t len) {
  for (int i = 0; i < vm->nslot; i++) {
    struct memslot *s = &vm->mem[i];
    if (gpa >= s->phys && gpa + len <= s->phys + s->size)
      return (char *)s->mem + (gpa - s->phys);
  }
  return NULL;
}

static void *guest_virt(struct vcpu *vcpu, uint32_t va, size_t len) {
  struct vm *vm = vcpu->vm;
  struct vcpu_sregs sregs;
  B->get_sregs(vcpu, &sregs);
  if (!(sregs.cr0 & (1u << 31)))
    return guest_phys(vm, va, len);
  uint32_t *pgdir = guest_phys(vm, sregs.cr3 & 0xfffff000, 4096);
  if (!pgdir)
    return NULL;

  uint32_t pde = pgdir[va >> 22];
  if (!(pde & 1))
    return NULL;

  if (pde & 0x80) {
    /* 4MB PSE page */
    ulong pa = (pde & 0xffc00000) | (va & 0x3fffff);
    return guest_phys(vm, pa, len);
  }

  uint32_t *pgtab = guest_phys(vm, pde & 0xfffff000, 4096);
  if (!pgtab)
    return NULL;

  uint32_t pte = pgtab[(va >> 12) & 0x3ff];
  if (!(pte & 1))
    return NULL;

  ulong pa = (pte & 0xfffff000) | (va & 0xfff);
  return guest_phys(vm, pa, len);
}

static void dump_seg(const char *name, struct vcpu_segment *s) {
  fprintf(stderr, "  %s: sel=0x%04x base=0x%lx limit=0x%x type=%u p=%u dpl=%u db=%u s=%u l=%u g=%u\n",
          name, s->selector, (ulong)s->base, s->limit, s->type,
          s->present, s->dpl, s->db, s->s, s->l, s->g);
}

static void dump_regs(struct vcpu *vcpu) {
  struct vcpu_regs regs;
  struct vcpu_sregs sregs;
  B->get_regs(vcpu, &regs);
  B->get_sregs(vcpu, &sregs);
  fprintf(stderr, "--- vcpu registers ---\n");
  fprintf(stderr, "  eax=0x%08x  ebx=0x%08x  ecx=0x%08x  edx=0x%08x\n",
          (u32)regs.rax, (u32)regs.rbx, (u32)regs.rcx, (u32)regs.rdx);
  fprintf(stderr, "  esi=0x%08x  edi=0x%08x  esp=0x%08x  ebp=0x%08x\n",
          (u32)regs.rsi, (u32)regs.rdi, (u32)regs.rsp, (u32)regs.rbp);
  fprintf(stderr, "  eip=0x%08x  eflags=0x%08x\n",
          (u32)regs.rip, (u32)regs.rflags);
  fprintf(stderr, "  cr0=0x%08lx  cr2=0x%08lx  cr3=0x%08lx  cr4=0x%08lx\n",
          (ulong)sregs.cr0, (ulong)sregs.cr2, (ulong)sregs.cr3, (ulong)sregs.cr4);
  dump_seg("cs", &sregs.cs);
  dump_seg("ds", &sregs.ds);
  dump_seg("es", &sregs.es);
  dump_seg("ss", &sregs.ss);
  dump_seg("fs", &sregs.fs);
  dump_seg("gs", &sregs.gs);
  /* dump bytes at EIP */
  void *ip = guest_virt(vcpu, (uint32_t)regs.rip, 16);
  if (ip) {
    fprintf(stderr, "  code:");
    for (int i = 0; i < 16; i++)
      fprintf(stderr, " %02x", ((u8 *)ip)[i]);
    fprintf(stderr, "\n");
  }
}

static void print_stacktrace(struct vcpu *vcpu, struct vcpu_regs *regs) {
  uint32_t ebp = (uint32_t)regs->rbp;
  uint32_t *fp;
  fprintf(stderr, "stack trace:\n");
  fprintf(stderr, "  0x%lx\n", (ulong)regs->rip);
  for (int depth = 0; depth < 20 && ebp; depth++) {
    fp = guest_virt(vcpu, ebp, 8);
    if (!fp)
      break;
    fprintf(stderr, "  0x%x\n", fp[1]);
    ebp = fp[0];
  }
}

struct vcpu *create_vcpu(struct vm *vm, int id) {
  struct vcpu *vcpu;
  vcpu = malloc(sizeof *vcpu);
  if (!vcpu)
    return NULL;
  memset(vcpu, 0, sizeof *vcpu);
  vcpu->vm = vm;
  vcpu->id = id;
  vcpu->priv = B->vcpu_backend(vm, id);
  if (!vcpu->priv) {
    free(vcpu);
    return NULL;
  }
  return vcpu;
}

static void *vcpu_run(void *arg) {
  struct vcpu *vcpu = arg;
  struct vm *vm = vcpu->vm;
  struct vcpu_exit e;

  current = vcpu;

  while (!vcpu->online)
    ;

  while (!vcpu->exit) {
    struct vcpu_regs regs;
    lapictimer_tick();
    if (B->vcpu_run(vcpu, &e) < 0)
      break;
    B->get_regs(vcpu, &regs);
    if (vcpu->dump) {
      dump_regs(vcpu);
      print_stacktrace(vcpu, &regs);
      vcpu->dump = 0;
    }
    switch (e.reason) {
    case VCPU_EXIT_IO:
      for (u32 i = 0; i < e.io.count; i++) {
        void *d = (u8 *)e.io.data + i * e.io.size;
        if (e.io.direction == VCPU_EXIT_IO_OUT) {
          if (port_dev_write(vm->pio, e.io.port, d, e.io.size) < 0)
            fprintf(stderr, "unhandled pio out: pc=0x%lx port=0x%x size=%u\n",
                    (ulong)regs.rip, e.io.port, e.io.size);
        } else {
          if (port_dev_read(vm->pio, e.io.port, d, e.io.size) < 0)
            fprintf(stderr, "unhandled pio in:  pc=0x%lx port=0x%x size=%u\n",
                    (ulong)regs.rip, e.io.port, e.io.size);
        }
      }
      break;
    case VCPU_EXIT_MMIO:
      if (e.mmio.is_write) {
        if (mmio_dev_write(vm->mmio, e.mmio.addr, e.mmio.data, e.mmio.len) < 0)
          fprintf(stderr, "unhandled mmio write: pc=0x%lx addr=0x%lx len=%u\n",
                  (ulong)regs.rip, (ulong)e.mmio.addr, e.mmio.len);
      } else {
        if (mmio_dev_read(vm->mmio, e.mmio.addr, e.mmio.data, e.mmio.len) < 0)
          fprintf(stderr, "unhandled mmio read: pc=0x%lx addr=0x%lx len=%u\n",
                  (ulong)regs.rip, (ulong)e.mmio.addr, e.mmio.len);
      }
      break;
    case VCPU_EXIT_DEBUG:
      dump_regs(vcpu);
      break;
    case VCPU_EXIT_IRQ_WINDOW:
    case VCPU_EXIT_INTR:
      break;
    case VCPU_EXIT_HLT:
    case VCPU_EXIT_SHUTDOWN:
    case VCPU_EXIT_SYSTEM_EVENT:
      fprintf(stderr, "vcpu exit: pc=0x%lx reason=%d\n",
              (ulong)regs.rip, e.reason);
      dump_regs(vcpu);
      print_stacktrace(vcpu, &regs);
      vcpu->exit = 1;
      break;
    case VCPU_EXIT_INTERNAL_ERROR:
      fprintf(stderr, "internal error: pc=0x%lx suberror=%u\n",
              (ulong)regs.rip, e.internal.suberror);
      dump_regs(vcpu);
      vcpu->exit = 1;
      break;
    default:
      fprintf(stderr, "unhandled vcpu exit: pc=0x%lx reason=%d\n",
              (ulong)regs.rip, e.reason);
      vcpu->exit = 1;
      break;
    }
  }
  vcpu->online = 0;
  return NULL;
}

int vcpu_kick(struct vcpu *vcpu) {
  return pthread_create(&vcpu->thread, NULL, vcpu_run, vcpu);
}

void vcpu_wait(struct vcpu *vcpu) {
  pthread_join(vcpu->thread, NULL);
}

int vcpu_setup_bios(struct vcpu *vcpu) {
  struct vcpu_sregs sregs;
  struct vcpu_regs regs;
  struct vcpu_segment dataseg = {
    .base     = 0,
    .limit    = 0xffff,
    .selector = 0,
    .type     = 0x3,
    .present  = 1,
    .dpl      = 0,
    .db       = 0,
    .s        = 1,
    .g        = 0,
  };
  if (B->get_sregs(vcpu, &sregs) < 0)
    return -1;
  sregs.cs.base = 0xffff0000;
  sregs.cs.limit = 0xffff;
  sregs.cs.selector = 0xf000;
  sregs.cs.type = 0xb;
  sregs.cs.present = 1;
  sregs.cs.dpl = 0;
  sregs.cs.db = 0;
  sregs.cs.s = 1;
  sregs.cs.l = 0;
  sregs.cs.g = 0;
  sregs.ds = dataseg;
  sregs.es = dataseg;
  sregs.fs = dataseg;
  sregs.gs = dataseg;
  sregs.ss = dataseg;
  sregs.cr0 &= ~1u;  /* PE off — real mode */
  if (B->set_sregs(vcpu, &sregs) < 0)
    return -1;
  memset(&regs, 0, sizeof regs);
  regs.rip = 0xfff0;
  regs.rflags = 0x2;
  return B->set_regs(vcpu, &regs);
}

int vcpu_set_singlestep(struct vcpu *vcpu, int enable) {
  return B->set_debug(vcpu, enable);
}

int vcpu_setup_boot(struct vcpu *vcpu, ulong entry, ulong sp) {
  struct vcpu_sregs sregs;
  struct vcpu_regs regs;
  struct vcpu_segment dataseg = {
    .base     = 0,
    .limit    = 0xffffffff,
    .selector = 0x10,
    .type     = 0x3,
    .present  = 1,
    .dpl      = 0,
    .db       = 1,
    .s        = 1,
    .g        = 1,
  };

  if (B->get_sregs(vcpu, &sregs) < 0)
    return -1;
  sregs.cs.base     = 0;
  sregs.cs.limit    = 0xffffffff;
  sregs.cs.selector = 0x08;
  sregs.cs.type     = 0xb;
  sregs.cs.present  = 1;
  sregs.cs.dpl      = 0;
  sregs.cs.db       = 1;
  sregs.cs.s        = 1;
  sregs.cs.l        = 0;
  sregs.cs.g        = 1;
  sregs.ds = dataseg;
  sregs.es = dataseg;
  sregs.fs = dataseg;
  sregs.gs = dataseg;
  sregs.ss = dataseg;
  sregs.cr0 |= 1;
  if (B->set_sregs(vcpu, &sregs) < 0)
    return -1;
  memset(&regs, 0, sizeof regs);
  regs.rip = entry;
  regs.rsp = sp;
  regs.rflags = 0x2;
  return B->set_regs(vcpu, &regs);
}
