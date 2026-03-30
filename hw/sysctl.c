#include <stdlib.h>
#include <string.h>
#include "hw/device.h"
#include "hw/pio.h"

#define SYSCTL_PORT  0x92
#define SYSCTL_END   0x93

struct sysctl {
  u8 val;
};

static int sysctl_read(struct device *dev, u32 offset, void *d, u32 size) {
  struct sysctl *s = dev->priv;
  (void)offset;
  (void)size;
  *(u8 *)d = s->val;
  return 0;
}

static int sysctl_write(struct device *dev, u32 offset, void *d, u32 size) {
  struct sysctl *s = dev->priv;
  (void)offset;
  (void)size;
  s->val = *(u8 *)d & ~1u;  /* bit 0 (reset) is write-only pulse, don't latch */
  return 0;
}

void sysctl_init(struct pio_bus *bus) {
  struct sysctl *s;
  struct device *dev;

  s = malloc(sizeof *s);
  if (!s)
    return;
  memset(s, 0, sizeof *s);

  dev = new_device(sysctl_read, sysctl_write, s);
  if (!dev) {
    free(s);
    return;
  }
  pio_register(bus, SYSCTL_PORT, SYSCTL_END, dev);
}
