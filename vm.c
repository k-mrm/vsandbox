#include <stdlib.h>
#include <string.h>
#include "vm.h"
#include "vcpu.h"
#include "backend.h"

struct vm *create_vm(struct vmconfig *cfg) {
  struct vm *vm;
  vm = malloc(sizeof *vm);
  if (!vm)
    return NULL;
  memset(vm, 0, sizeof *vm);
  vm->memsz = cfg->memsz;
  vm->priv = B->vm_backend();
  if (!vm->priv)
    return NULL;
  for (uint i = 0; i < cfg->ncpu && i < 16; i++) {
    vm->vcpu[i] = create_vcpu(vm, i);
    if (!vm->vcpu[i])
      return NULL;
    vm->ncpu++;
  }
  vm->pio = create_pio_bus();
  if (!vm->pio)
    return NULL;
  vm->mmio = create_mmio_bus();
  if (!vm->mmio)
    return NULL;
  legacy_serial(vm->pio);
  pit_init(vm->pio);
  cmos_init(vm->pio, cfg->memsz >> 20);
  sysctl_init(vm->pio);
  debugcon_init(vm->pio);
  pci_init(vm->pio);
  dma_init(vm->pio);
  pic_init(vm->pio);
  kbd_init(vm->pio);
  ata_init(vm->pio);
  lpt_init(vm->pio);
  vga_init(vm->pio);
  ioapic_init(vm->mmio);
  lapic_init(vm->mmio);
  vm->fw_cfg = fw_cfg_init(vm);
  vm->vcpu[0]->online = 1;
  return vm;
}

int vm_mapmem(struct vm *vm, ulong gpa, void *hva, ulong size) {
  int slot = vm->nslot;
  if (slot >= 16)
    panic("too many memslots");
  vm->mem[slot].phys = gpa;
  vm->mem[slot].mem = hva;
  vm->mem[slot].size = size;
  vm->nslot++;
  return B->mapmem(vm, gpa, hva, size, slot);
}
