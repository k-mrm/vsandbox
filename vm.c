#include <stdlib.h>
#include <string.h>
#include "vm.h"
#include "vcpu.h"
#include "backend.h"
#include "device.h"

static int sink_read(struct device *dev, u32 off, void *d, u32 sz) {
  (void)dev; (void)off; (void)sz;
  *(u8 *)d = 0;
  return 0;
}
static int sink_write(struct device *dev, u32 off, void *d, u32 sz) {
  (void)dev; (void)off; (void)d; (void)sz;
  return 0;
}

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
  pci_init(vm->pio);
  dma_init(vm->pio);
  pic_init(vm->pio);
  kbd_init(vm->pio);
  ata_init(vm->pio);
  lpt_init(vm->pio);
  vga_init(vm->pio);
  pio_register(vm->pio, 0x402, 0x403,
               new_device(sink_read, sink_write, NULL));
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
