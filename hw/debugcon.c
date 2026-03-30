#include <stdlib.h>
#include <unistd.h>
#include "hw/device.h"
#include "hw/pio.h"

#define DEBUGCON_PORT  0x402
#define DEBUGCON_END   0x403

static int debugcon_read(struct device *dev, u32 offset, void *d, u32 size) {
  (void)dev;
  (void)offset;
  (void)size;
  *(u8 *)d = 0xe9;  /* magic: debugcon present */
  return 0;
}

static int debugcon_write(struct device *dev, u32 offset, void *d, u32 size) {
  (void)dev;
  (void)offset;
  (void)size;
  if (write(STDERR_FILENO, d, 1) < 0)
    return -1;
  return 0;
}

void debugcon_init(struct pio_bus *bus) {
  struct device *dev;

  dev = new_device(debugcon_read, debugcon_write, NULL);
  if (!dev)
    return;
  pio_register(bus, DEBUGCON_PORT, DEBUGCON_END, dev);
}
