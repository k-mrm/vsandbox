#include <stdlib.h>
#include "device.h"
#include "pio.h"

/* LPT1: 0x378-0x37F, LPT2: 0x278-0x27F */
#define LPT1_BASE  0x278
#define LPT1_END   0x280
#define LPT2_BASE  0x378
#define LPT2_END   0x380

static int lpt_read(struct device *dev, u32 offset, void *d, u32 size) {
  (void)dev; (void)offset; (void)size;
  *(u8 *)d = 0;
  return 0;
}

static int lpt_write(struct device *dev, u32 offset, void *d, u32 size) {
  (void)dev; (void)offset; (void)d; (void)size;
  return 0;
}

void lpt_init(struct pio_bus *bus) {
  struct device *d1, *d2;

  d1 = new_device(lpt_read, lpt_write, NULL);
  d2 = new_device(lpt_read, lpt_write, NULL);
  if (!d1 || !d2)
    return;
  pio_register(bus, LPT1_BASE, LPT1_END, d1);
  pio_register(bus, LPT2_BASE, LPT2_END, d2);
}
