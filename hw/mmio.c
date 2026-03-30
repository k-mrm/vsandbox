#include <stdlib.h>
#include <string.h>
#include "hw/mmio.h"

struct mmio_bus *create_mmio_bus(void) {
  struct mmio_bus *bus;
  bus = malloc(sizeof *bus);
  if (!bus)
    return NULL;
  bus->head = NULL;
  return bus;
}

int mmio_register(struct mmio_bus *bus, ulong base, ulong end, struct device *d) {
  struct mmio_dev *dev;
  dev = malloc(sizeof *dev);
  if (!dev)
    return -1;
  dev->base = base;
  dev->end = end;
  dev->dev = d;
  dev->next = bus->head;
  bus->head = dev;
  return 0;
}

static struct mmio_dev *mmio_find(struct mmio_bus *bus, ulong addr) {
  struct mmio_dev *dev;
  for (dev = bus->head; dev; dev = dev->next) {
    if (addr >= dev->base && addr < dev->end)
      return dev;
  }
  return NULL;
}

int mmio_dev_read(struct mmio_bus *bus, ulong addr, void *data, u32 size) {
  struct mmio_dev *md = mmio_find(bus, addr);
  if (!md || !md->dev->read)
    return -1;
  return md->dev->read(md->dev, addr - md->base, data, size);
}

int mmio_dev_write(struct mmio_bus *bus, ulong addr, void *data, u32 size) {
  struct mmio_dev *md = mmio_find(bus, addr);
  if (!md || !md->dev->write)
    return -1;
  return md->dev->write(md->dev, addr - md->base, data, size);
}
