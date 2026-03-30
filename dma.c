#include <stdlib.h>
#include "device.h"
#include "pio.h"

/* DMA controller ports: 0x00-0x0F (primary), 0xC0-0xDF (secondary),
   page registers 0x80-0x8F, plus misc 0xD0-0xDA */

#define DMA1_BASE  0x00
#define DMA1_END   0x10
#define DMA2_BASE  0xC0
#define DMA2_END   0xE0
#define DMA_PAGE_BASE 0x80
#define DMA_PAGE_END  0x90

static int dma_read(struct device *dev, u32 offset, void *d, u32 size) {
  (void)dev; (void)offset; (void)size;
  *(u8 *)d = 0;
  return 0;
}

static int dma_write(struct device *dev, u32 offset, void *d, u32 size) {
  (void)dev; (void)offset; (void)d; (void)size;
  return 0;
}

void dma_init(struct pio_bus *bus) {
  struct device *d1, *d2, *dp;

  d1 = new_device(dma_read, dma_write, NULL);
  d2 = new_device(dma_read, dma_write, NULL);
  dp = new_device(dma_read, dma_write, NULL);
  if (!d1 || !d2 || !dp)
    return;
  pio_register(bus, DMA1_BASE, DMA1_END, d1);
  pio_register(bus, DMA2_BASE, DMA2_END, d2);
  pio_register(bus, DMA_PAGE_BASE, DMA_PAGE_END, dp);
}
