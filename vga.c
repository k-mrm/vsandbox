#include <stdlib.h>
#include <string.h>
#include "device.h"
#include "pio.h"

#define VGA_CRTC_ADDR   0x3D4
#define VGA_CRTC_END    0x3D6

#define CRTC_NUM_REGS   25

struct vga {
  u8 index;
  u8 regs[CRTC_NUM_REGS];
};

static int vga_read(struct device *dev, u32 offset, void *d, u32 size) {
  struct vga *v = dev->priv;
  (void)size;
  if (offset == 0)
    *(u8 *)d = v->index;
  else
    *(u8 *)d = (v->index < CRTC_NUM_REGS) ? v->regs[v->index] : 0;
  return 0;
}

static int vga_write(struct device *dev, u32 offset, void *d, u32 size) {
  struct vga *v = dev->priv;
  u8 val = *(u8 *)d;
  (void)size;
  if (offset == 0)
    v->index = val;
  else if (v->index < CRTC_NUM_REGS)
    v->regs[v->index] = val;
  return 0;
}

void vga_init(struct pio_bus *bus) {
  struct vga *v;
  struct device *dev;

  v = malloc(sizeof *v);
  if (!v)
    return;
  memset(v, 0, sizeof *v);

  dev = new_device(vga_read, vga_write, v);
  if (!dev) {
    free(v);
    return;
  }
  pio_register(bus, VGA_CRTC_ADDR, VGA_CRTC_END, dev);
}
