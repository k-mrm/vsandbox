#include <stdlib.h>
#include <string.h>
#include "hw/device.h"

struct device *new_device(int (*read)(struct device *, u32, void *, u32),
                          int (*write)(struct device *, u32, void *, u32),
                          void *priv) {
  struct device *dev;
  dev = malloc(sizeof *dev);
  if (!dev)
    return NULL;
  dev->read = read;
  dev->write = write;
  dev->priv = priv;
  return dev;
}
