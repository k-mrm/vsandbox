#include <stdlib.h>
#include <string.h>
#include "hw/device.h"
#include "hw/pio.h"

#define PCI_ADDR_PORT   0xCF8
#define PCI_PORT_END    0xD00

#define PCI_ADDR_ENABLE (1u << 31)
#define PCI_BUS(a)      (((a) >> 16) & 0xff)
#define PCI_DEV(a)      (((a) >> 11) & 0x1f)
#define PCI_FN(a)       (((a) >> 8)  & 7)
#define PCI_REG(a)      ((a) & 0xfc)

/* i440FX host bridge config space (256 bytes) */
static u8 host_cfg[256] = {
  0x86, 0x80, 0x37, 0x12,  /* 00: vendor=8086 device=1237 */
  0x06, 0x00, 0x00, 0x00,  /* 04: command, status */
  0x02, 0x00, 0x00, 0x06,  /* 08: rev=02, class=060000 (host bridge) */
};

struct pci {
  u32 addr;
};

static u32 pci_cfg_read(struct pci *p, u32 offset, u32 size) {
  if (!(p->addr & PCI_ADDR_ENABLE))
    return 0xffffffff;

  /* only 0:0.0 exists */
  if (PCI_BUS(p->addr) || PCI_DEV(p->addr) || PCI_FN(p->addr))
    return 0xffffffff;

  u8 reg = PCI_REG(p->addr) + (offset - 4);
  if (reg + size > sizeof(host_cfg))
    return 0xffffffff;

  u32 val = 0;
  memcpy(&val, &host_cfg[reg], size);
  return val;
}

static void pci_cfg_write(struct pci *p, u32 offset, void *d, u32 size) {
  if (!(p->addr & PCI_ADDR_ENABLE))
    return;
  if (PCI_BUS(p->addr) || PCI_DEV(p->addr) || PCI_FN(p->addr))
    return;

  u8 reg = PCI_REG(p->addr) + (offset - 4);
  if (reg + size > sizeof(host_cfg))
    return;
  /* host bridge has no BARs — ignore writes to BAR range and expansion ROM */
  if ((reg >= 0x10 && reg < 0x28) || (reg >= 0x30 && reg < 0x34))
    return;
  memcpy(&host_cfg[reg], d, size);
}

static int pci_read(struct device *dev, u32 offset, void *d, u32 size) {
  struct pci *p = dev->priv;

  if (offset == 0) {
    memcpy(d, &p->addr, size < 4 ? size : 4);
  } else if (offset >= 4 && offset < 8) {
    u32 val = pci_cfg_read(p, offset, size);
    memcpy(d, &val, size < 4 ? size : 4);
  }
  return 0;
}

static int pci_write(struct device *dev, u32 offset, void *d, u32 size) {
  struct pci *p = dev->priv;

  if (offset == 0 && size == 4)
    memcpy(&p->addr, d, 4);
  else if (offset >= 4 && offset < 8)
    pci_cfg_write(p, offset, d, size);
  return 0;
}

void pci_init(struct pio_bus *bus) {
  struct pci *p;
  struct device *dev;

  p = malloc(sizeof *p);
  if (!p)
    return;
  memset(p, 0, sizeof *p);

  dev = new_device(pci_read, pci_write, p);
  if (!dev) {
    free(p);
    return;
  }
  pio_register(bus, PCI_ADDR_PORT, PCI_PORT_END, dev);
}
