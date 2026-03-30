#ifndef _VS_PIO_H
#define _VS_PIO_H

#include "vsandbox.h"
#include "hw/device.h"

struct pio_dev {
  struct pio_dev *next;
  u16 portbase;
  u16 portend;
  struct device *dev;
};

struct pio_bus {
  struct pio_dev *head;
};

struct pio_bus *create_pio_bus(void);
int pio_register(struct pio_bus *bus, u16 portb, u16 porte, struct device *dev);
int port_dev_read(struct pio_bus *bus, u16 port, void *data, u32 size);
int port_dev_write(struct pio_bus *bus, u16 port, void *data, u32 size);

#endif  // _VS_PIO_H
