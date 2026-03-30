#ifndef _VS_MMIO_H
#define _VS_MMIO_H

#include "vsandbox.h"
#include "device.h"

struct mmio_dev {
  struct mmio_dev *next;
  ulong base;
  ulong end;
  struct device *dev;
};

struct mmio_bus {
  struct mmio_dev *head;
};

struct mmio_bus *create_mmio_bus(void);
int mmio_register(struct mmio_bus *bus, ulong base, ulong end, struct device *dev);
int mmio_dev_read(struct mmio_bus *bus, ulong addr, void *data, u32 size);
int mmio_dev_write(struct mmio_bus *bus, ulong addr, void *data, u32 size);

#endif  // _VS_MMIO_H
