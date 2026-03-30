#include <stdlib.h>
#include <stdio.h>
#include "device.h"
#include "pio.h"

/* primary 0x1F0-0x1F7, control 0x3F6-0x3F7 */
/* secondary 0x170-0x177, control 0x376-0x377 */
#define ATA0_BASE  0x1F0
#define ATA0_END   0x1F8
#define ATA0_CTRL  0x3F6
#define ATA0_CEND  0x3F8
#define ATA1_BASE  0x170
#define ATA1_END   0x178
#define ATA1_CTRL  0x376
#define ATA1_CEND  0x378

/* offset 7 = status register */
#define ATA_OFF_STATUS  7

struct ata {
  u16 base;
};

static int ata_read(struct device *dev, u32 offset, void *d, u32 size) {
  struct ata *a = dev->priv;
  u8 val;
  (void)size;
  if (offset == ATA_OFF_STATUS)
    val = 0x40;  /* DRDY=1, no drive busy */
  else
    val = 0x7f;  /* floating bus */
  *(u8 *)d = val;
  fprintf(stderr, "ata: read  port=0x%03x val=0x%02x\n",
          a->base + offset, val);
  return 0;
}

static int ata_write(struct device *dev, u32 offset, void *d, u32 size) {
  struct ata *a = dev->priv;
  u8 val = *(u8 *)d;
  (void)size;
  fprintf(stderr, "ata: write port=0x%03x val=0x%02x\n",
          a->base + offset, val);
  return 0;
}

static struct device *ata_dev(u16 base) {
  struct ata *a = malloc(sizeof *a);
  if (!a)
    return NULL;
  a->base = base;
  return new_device(ata_read, ata_write, a);
}

void ata_init(struct pio_bus *bus) {
  struct device *d0  = ata_dev(ATA0_BASE);
  struct device *d0c = ata_dev(ATA0_CTRL);
  struct device *d1  = ata_dev(ATA1_BASE);
  struct device *d1c = ata_dev(ATA1_CTRL);
  if (!d0 || !d0c || !d1 || !d1c)
    return;
  pio_register(bus, ATA0_BASE, ATA0_END, d0);
  pio_register(bus, ATA0_CTRL, ATA0_CEND, d0c);
  pio_register(bus, ATA1_BASE, ATA1_END, d1);
  pio_register(bus, ATA1_CTRL, ATA1_CEND, d1c);
}
