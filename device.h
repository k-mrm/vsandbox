#ifndef _VS_DEVICE_H
#define _VS_DEVICE_H

#include "vsandbox.h"

struct device {
  int (*read)(struct device *dev, u32 offset, void *d, u32 size);
  int (*write)(struct device *dev, u32 offset, void *d, u32 size);
  void *priv;
};

struct device *new_device(int (*read)(struct device *, u32, void *, u32),
                          int (*write)(struct device *, u32, void *, u32),
                          void *priv);

#endif  // _VS_DEVICE_H
