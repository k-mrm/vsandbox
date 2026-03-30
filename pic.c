#include <stdlib.h>
#include <string.h>
#include "device.h"
#include "pio.h"

#define PIC_MASTER_BASE  0x20
#define PIC_MASTER_END   0x22
#define PIC_SLAVE_BASE   0xA0
#define PIC_SLAVE_END    0xA2

struct pic_chip {
  u8 icw_step;   /* 0=idle, 1-4=expecting ICWn */
  u8 icw4_needed;
  u8 imr;        /* interrupt mask register */
  u8 irr;
  u8 isr;
  u8 vector_base;
  u8 cascade;
  u8 icw4;
  u8 read_isr;   /* OCW3: next read returns ISR instead of IRR */
};

static int pic_read(struct device *dev, u32 offset, void *d, u32 size) {
  struct pic_chip *p = dev->priv;
  (void)size;
  if (offset == 0) {
    *(u8 *)d = p->read_isr ? p->isr : p->irr;
  } else {
    *(u8 *)d = p->imr;
  }
  return 0;
}

static int pic_write(struct device *dev, u32 offset, void *d, u32 size) {
  struct pic_chip *p = dev->priv;
  u8 val = *(u8 *)d;
  (void)size;

  if (offset == 0) {
    if (val & 0x10) {
      /* ICW1 */
      p->icw_step = 2;
      p->icw4_needed = val & 1;
      p->imr = 0;
      p->isr = 0;
      p->irr = 0;
      p->read_isr = 0;
    } else if (val & 0x08) {
      /* OCW3 */
      if (val & 0x02)
        p->read_isr = val & 1;
    } else {
      /* OCW2 (EOI etc) */
      if ((val & 0xe0) == 0x20) {
        /* non-specific EOI: clear highest ISR bit */
        for (int i = 0; i < 8; i++) {
          if (p->isr & (1 << i)) {
            p->isr &= ~(1 << i);
            break;
          }
        }
      }
    }
  } else {
    /* data port */
    switch (p->icw_step) {
    case 2:
      p->vector_base = val & 0xf8;
      p->icw_step = 3;
      break;
    case 3:
      p->cascade = val;
      p->icw_step = p->icw4_needed ? 4 : 0;
      break;
    case 4:
      p->icw4 = val;
      p->icw_step = 0;
      break;
    default:
      /* OCW1: set IMR */
      p->imr = val;
      break;
    }
  }
  return 0;
}

static struct pic_chip *new_pic(void) {
  struct pic_chip *p = malloc(sizeof *p);
  if (!p)
    return NULL;
  memset(p, 0, sizeof *p);
  p->imr = 0xff;
  return p;
}

void pic_init(struct pio_bus *bus) {
  struct pic_chip *master, *slave;
  struct device *dm, *ds;

  master = new_pic();
  slave = new_pic();
  if (!master || !slave)
    return;
  dm = new_device(pic_read, pic_write, master);
  ds = new_device(pic_read, pic_write, slave);
  if (!dm || !ds)
    return;
  pio_register(bus, PIC_MASTER_BASE, PIC_MASTER_END, dm);
  pio_register(bus, PIC_SLAVE_BASE, PIC_SLAVE_END, ds);
}
