#ifndef _VS_VM_H
#define _VS_VM_H

#include "vsandbox.h"
#include "pio.h"
#include "mmio.h"

struct vcpu;
struct fw_cfg;

struct vmconfig {
  uint ncpu;
  uint memsz;
};

struct memslot {
  void *mem;
  ulong phys;
  ulong size;
};

struct vm {
  struct vcpu *vcpu[16];
  int ncpu;
  ulong memsz;
  struct memslot mem[16];
  struct pio_bus *pio;
  struct mmio_bus *mmio;
  int nslot;
  void *priv;
  /* kernel image (for fw_cfg) */
  void *kernel_data;
  u32 kernel_size;
  u32 kernel_addr;
  u32 kernel_entry;
  char *kernel_cmdline;
  struct fw_cfg *fw_cfg;
};

struct vm *create_vm(struct vmconfig *cfg);
int vm_mapmem(struct vm *vm, ulong gpa, void *hva, ulong size);
void legacy_serial(struct pio_bus *bus);
void pit_init(struct pio_bus *bus);
void cmos_init(struct pio_bus *bus, u32 mem_mb);
void sysctl_init(struct pio_bus *bus);
void pci_init(struct pio_bus *bus);
void dma_init(struct pio_bus *bus);
void pic_init(struct pio_bus *bus);
void kbd_init(struct pio_bus *bus);
void ata_init(struct pio_bus *bus);
void lpt_init(struct pio_bus *bus);
void ioapic_init(struct mmio_bus *bus);
void lapic_init(struct mmio_bus *bus);
void lapictimer_tick(void);
int lapic_pending_vec(void);
void lapic_deliver(int vec);
void lapic_send_irq(int cpu, int vec);
void ioapic_raise_irq(int pin);
void kbd_push_key(u8 ascii);
struct fw_cfg *fw_cfg_init(struct vm *vm);
int fw_cfg_add_file(struct fw_cfg *cfg, const char *name,
                    void *data, u32 len);
void vga_init(struct pio_bus *bus);

#endif  // _VS_VM_H
