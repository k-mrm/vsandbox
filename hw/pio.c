#include <stdlib.h>
#include <string.h>
#include "hw/pio.h"

struct pio_bus *create_pio_bus(void) {
  struct pio_bus *bus;
  bus = malloc(sizeof *bus);
  if (!bus)
    return NULL;
  bus->head = NULL;
  return bus;
}

int pio_register(struct pio_bus *bus, u16 portb, u16 porte, struct device *d) {
  struct pio_dev *dev;
  dev = malloc(sizeof *dev);
  if (!dev)
    return -1;
  dev->portbase = portb;
  dev->portend = porte;
  dev->dev = d;
  dev->next = bus->head;
  bus->head = dev;
  return 0;
}

static struct pio_dev *port_dev(struct pio_bus *bus, u16 port) {
  struct pio_dev *dev;
  for (dev = bus->head; dev; dev = dev->next) {
    if (port >= dev->portbase && port < dev->portend)
      return dev;
  }
  return NULL;
}

int port_dev_read(struct pio_bus *bus, u16 port, void *data, u32 size) {
  struct pio_dev *pd = port_dev(bus, port);
  if (!pd || !pd->dev->read)
    return -1;
  return pd->dev->read(pd->dev, port - pd->portbase, data, size);
}

int port_dev_write(struct pio_bus *bus, u16 port, void *data, u32 size) {
  struct pio_dev *pd = port_dev(bus, port);
  if (!pd || !pd->dev->write)
    return -1;
  return pd->dev->write(pd->dev, port - pd->portbase, data, size);
}
