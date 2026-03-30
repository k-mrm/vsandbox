#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "vm.h"
#include "hw/device.h"
#include "hw/mmio.h"

#define IOAPIC_BASE       0xFEC00000UL
#define IOAPIC_SIZE       0x1000

#define IOREGSEL          0x00
#define IOWIN             0x10

/* indirect registers */
#define IOAPIC_ID         0x00
#define IOAPIC_VER        0x01
#define IOAPIC_ARB        0x02
#define IOREDTBL_BASE     0x10
#define IOAPIC_NUM_PINS   24

struct ioapic {
  u32 ioregsel;
  u32 id;
  u64 redtbl[IOAPIC_NUM_PINS];
};

static u32 ioapic_read_indirect(struct ioapic *s) {
  switch (s->ioregsel) {
  case IOAPIC_ID:
    return s->id;
  case IOAPIC_VER:
    return ((IOAPIC_NUM_PINS - 1) << 16) | 0x11;
  case IOAPIC_ARB:
    return 0;
  default:
    if (s->ioregsel >= IOREDTBL_BASE &&
        s->ioregsel < IOREDTBL_BASE + IOAPIC_NUM_PINS * 2) {
      int pin = (s->ioregsel - IOREDTBL_BASE) / 2;
      int hi  = (s->ioregsel - IOREDTBL_BASE) & 1;
      if (hi)
        return (u32)(s->redtbl[pin] >> 32);
      return (u32)s->redtbl[pin];
    }
    return 0;
  }
}

static void ioapic_write_indirect(struct ioapic *s, u32 val) {
  switch (s->ioregsel) {
  case IOAPIC_ID:
    s->id = val & 0x0f000000;
    break;
  default:
    if (s->ioregsel >= IOREDTBL_BASE &&
        s->ioregsel < IOREDTBL_BASE + IOAPIC_NUM_PINS * 2) {
      int pin = (s->ioregsel - IOREDTBL_BASE) / 2;
      int hi  = (s->ioregsel - IOREDTBL_BASE) & 1;
      if (hi)
        s->redtbl[pin] = (s->redtbl[pin] & 0xFFFFFFFF) | ((u64)val << 32);
      else
        s->redtbl[pin] = (s->redtbl[pin] & 0xFFFFFFFF00000000ULL) | val;
    }
    break;
  }
}

static int ioapic_read(struct device *dev, u32 offset, void *d, u32 size) {
  struct ioapic *s = dev->priv;
  u32 val = 0;

  (void)size;
  switch (offset) {
  case IOREGSEL:
    val = s->ioregsel;
    break;
  case IOWIN:
    val = ioapic_read_indirect(s);
    break;
  }
  *(u32 *)d = val;
  return 0;
}

static int ioapic_write(struct device *dev, u32 offset, void *d, u32 size) {
  struct ioapic *s = dev->priv;
  u32 val = *(u32 *)d;

  (void)size;
  switch (offset) {
  case IOREGSEL:
    s->ioregsel = val;
    break;
  case IOWIN:
    ioapic_write_indirect(s, val);
    break;
  }
  return 0;
}

static struct ioapic *ioapicdev;

void ioapic_raise_irq(int pin) {
  u64 entry;
  int vec, dest;
  if (!ioapicdev || pin < 0 || pin >= IOAPIC_NUM_PINS)
    return;
  entry = ioapicdev->redtbl[pin];
  if (entry & (1ULL << 16))
    return;  /* masked */
  vec = entry & 0xff;
  dest = (entry >> 56) & 0xff;
  lapic_send_irq(dest, vec);
}

void ioapic_init(struct mmio_bus *bus) {
  struct ioapic *s;
  struct device *dev;

  s = malloc(sizeof *s);
  if (!s)
    return;
  memset(s, 0, sizeof *s);
  /* all redirection entries masked by default */
  for (int i = 0; i < IOAPIC_NUM_PINS; i++)
    s->redtbl[i] = (1ULL << 16);

  ioapicdev = s;
  dev = new_device(ioapic_read, ioapic_write, s);
  if (!dev) {
    free(s);
    ioapicdev = NULL;
    return;
  }
  mmio_register(bus, IOAPIC_BASE, IOAPIC_BASE + IOAPIC_SIZE, dev);
}
