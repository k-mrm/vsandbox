#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "hw/device.h"
#include "hw/pio.h"

#define SERIAL_PORT_SIZE  8
#define NUM_COM_PORTS     4

static const u16 com_base[NUM_COM_PORTS] = { 0x3f8, 0x2f8, 0x3e8, 0x2e8 };

/* 8250 register offsets */
#define REG_THR 0  /* write: transmit holding register */
#define REG_RBR 0  /* read:  receive buffer register */
#define REG_DLL 0  /* DLAB=1: divisor latch low */
#define REG_IER 1  /* interrupt enable register */
#define REG_DLM 1  /* DLAB=1: divisor latch high */
#define REG_IIR 2  /* read:  interrupt identification */
#define REG_FCR 2  /* write: FIFO control */
#define REG_LCR 3  /* line control register */
#define REG_MCR 4  /* modem control register */
#define REG_LSR 5  /* line status register */
#define REG_MSR 6  /* modem status register */
#define REG_SCR 7  /* scratch register */

#define LCR_DLAB   0x80
#define LSR_THRE   0x20
#define LSR_TEMT   0x40
#define IIR_NO_INT 0x01

struct x86serial {
  u8 ier;
  u8 iir;
  u8 lcr;
  u8 mcr;
  u8 lsr;
  u8 msr;
  u8 scr;
  u8 dll;
  u8 dlm;
  int out_fd;
  struct device *dev;
};

static int serial_read(struct device *dev, u32 offset, void *d, u32 size) {
  struct x86serial *s = dev->priv;
  u8 val = 0;

  (void)size;
  switch (offset) {
  case REG_RBR:
    val = (s->lcr & LCR_DLAB) ? s->dll : 0;
    break;
  case REG_IER:
    val = (s->lcr & LCR_DLAB) ? s->dlm : s->ier;
    break;
  case REG_IIR:
    val = s->iir;
    break;
  case REG_LCR:
    val = s->lcr;
    break;
  case REG_MCR:
    val = s->mcr;
    break;
  case REG_LSR:
    val = s->lsr;
    break;
  case REG_MSR:
    val = s->msr;
    break;
  case REG_SCR:
    val = s->scr;
    break;
  }
  *(u8 *)d = val;
  return 0;
}

static int serial_write(struct device *dev, u32 offset, void *d, u32 size) {
  struct x86serial *s = dev->priv;
  u8 val = *(u8 *)d;

  (void)size;
  switch (offset) {
  case REG_THR:
    if (s->lcr & LCR_DLAB) {
      s->dll = val;
    } else if (s->out_fd >= 0) {
      if (write(s->out_fd, &val, 1) < 0)
        return -1;
    }
    break;
  case REG_IER:
    if (s->lcr & LCR_DLAB)
      s->dlm = val;
    else
      s->ier = val;
    break;
  case REG_FCR:
    break;
  case REG_LCR:
    s->lcr = val;
    break;
  case REG_MCR:
    s->mcr = val;
    break;
  case REG_SCR:
    s->scr = val;
    break;
  default:
    break;
  }
  return 0;
}

void legacy_serial(struct pio_bus *bus) {
  struct x86serial *s;
  int i;

  if (!bus)
    return;
  for (i = 0; i < NUM_COM_PORTS; i++) {
    s = malloc(sizeof *s);
    if (!s)
      return;
    memset(s, 0, sizeof *s);
    s->lsr = LSR_THRE | LSR_TEMT;
    s->iir = IIR_NO_INT;
    s->out_fd = (i == 0) ? STDOUT_FILENO : -1;
    s->dev = new_device(serial_read, serial_write, s);
    if (!s->dev) {
      free(s);
      return;
    }
    pio_register(bus, com_base[i], com_base[i] + SERIAL_PORT_SIZE, s->dev);
  }
}
