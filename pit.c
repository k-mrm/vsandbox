#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "device.h"
#include "pio.h"

#define PIT_PORT_BASE     0x40
#define PIT_PORT_END      0x44   /* 0x40-0x43 */

#define PIT_FREQ          1193182UL
#define NUM_CHANNELS      3

/* command register bits */
#define CMD_CHANNEL(v)    (((v) >> 6) & 3)
#define CMD_ACCESS(v)     (((v) >> 4) & 3)
#define CMD_MODE(v)       (((v) >> 1) & 7)
#define CMD_BCD(v)        ((v) & 1)

#define ACCESS_LATCH      0
#define ACCESS_LO         1
#define ACCESS_HI         2
#define ACCESS_LOHI       3

struct pit_channel {
  u16 reload;          /* reload value written by guest */
  u16 latched;         /* latched counter snapshot */
  u8  access;          /* ACCESS_LO / ACCESS_HI / ACCESS_LOHI */
  u8  mode;
  u8  read_hi;         /* next read is high byte (LOHI toggle) */
  u8  write_hi;        /* next write is high byte */
  u8  latch_pending;   /* latched value waiting to be read */
  u64 load_ns;         /* clock_gettime when reload was written */
};

struct pit {
  struct pit_channel ch[NUM_CHANNELS];
};

static u64 now_ns(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (u64)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

/* compute current counter value from wall clock */
static u16 pit_counter(struct pit_channel *c) {
  u32 reload = c->reload ? c->reload : 0x10000;
  u64 elapsed = now_ns() - c->load_ns;
  /* ticks = elapsed * PIT_FREQ / 1e9 */
  u64 ticks = elapsed * PIT_FREQ / 1000000000ULL;
  u32 count = reload - (u32)(ticks % reload);
  if (count == reload)
    count = 0;
  return (u16)count;
}

static int pit_read(struct device *dev, u32 offset, void *d, u32 size) {
  struct pit *p = dev->priv;
  u8 val = 0;

  (void)size;
  if (offset >= NUM_CHANNELS) {
    *(u8 *)d = 0;
    return 0;
  }

  struct pit_channel *c = &p->ch[offset];
  u16 counter;

  if (c->latch_pending)
    counter = c->latched;
  else
    counter = pit_counter(c);

  switch (c->access) {
  case ACCESS_LO:
    val = counter & 0xff;
    c->latch_pending = 0;
    break;
  case ACCESS_HI:
    val = (counter >> 8) & 0xff;
    c->latch_pending = 0;
    break;
  case ACCESS_LOHI:
    if (!c->read_hi) {
      val = counter & 0xff;
      c->read_hi = 1;
    } else {
      val = (counter >> 8) & 0xff;
      c->read_hi = 0;
      c->latch_pending = 0;
    }
    break;
  }

  *(u8 *)d = val;
  return 0;
}

static int pit_write(struct device *dev, u32 offset, void *d, u32 size) {
  struct pit *p = dev->priv;
  u8 val = *(u8 *)d;

  (void)size;

  /* command register (offset 3 = port 0x43) */
  if (offset == 3) {
    u8 ch = CMD_CHANNEL(val);
    if (ch >= NUM_CHANNELS)
      return 0;
    struct pit_channel *c = &p->ch[ch];
    u8 acc = CMD_ACCESS(val);
    if (acc == ACCESS_LATCH) {
      c->latched = pit_counter(c);
      c->latch_pending = 1;
      c->read_hi = 0;
    } else {
      c->access = acc;
      c->mode = CMD_MODE(val);
      c->write_hi = 0;
      c->read_hi = 0;
    }
    return 0;
  }

  /* data port (offset 0-2) */
  if (offset >= NUM_CHANNELS)
    return 0;

  struct pit_channel *c = &p->ch[offset];
  switch (c->access) {
  case ACCESS_LO:
    c->reload = (c->reload & 0xff00) | val;
    c->load_ns = now_ns();
    break;
  case ACCESS_HI:
    c->reload = (c->reload & 0x00ff) | ((u16)val << 8);
    c->load_ns = now_ns();
    break;
  case ACCESS_LOHI:
    if (!c->write_hi) {
      c->reload = (c->reload & 0xff00) | val;
      c->write_hi = 1;
    } else {
      c->reload = (c->reload & 0x00ff) | ((u16)val << 8);
      c->write_hi = 0;
      c->load_ns = now_ns();
    }
    break;
  }
  return 0;
}

/* ── port 0x61 (system control port B) ─────────────── */

struct port61 {
  struct pit *pit;
  u8 val;
};

static int pit_out2(struct pit *p) {
  struct pit_channel *c = &p->ch[2];
  u32 reload = c->reload ? c->reload : 0x10000;
  u64 elapsed = now_ns() - c->load_ns;
  u64 ticks = elapsed * PIT_FREQ / 1000000000ULL;
  switch (c->mode) {
  case 0: return ticks >= reload;
  case 2: return (ticks % reload) != 1;
  case 3: return (ticks % reload) < (reload / 2);
  default: return 1;
  }
}

static int port61_read(struct device *dev, u32 offset, void *d, u32 size) {
  struct port61 *s = dev->priv;
  u8 val = s->val & 0x03;
  (void)offset; (void)size;
  /* bit 4: refresh toggle (~15us period) */
  if ((now_ns() / 15000) & 1)
    val |= 0x10;
  /* bit 5: timer 2 output */
  if (pit_out2(s->pit))
    val |= 0x20;
  *(u8 *)d = val;
  return 0;
}

static int port61_write(struct device *dev, u32 offset, void *d, u32 size) {
  struct port61 *s = dev->priv;
  u8 old = s->val;
  u8 val = *(u8 *)d;
  (void)offset; (void)size;
  s->val = val;
  /* gate 0→1 transition restarts channel 2 countdown */
  if (!(old & 1) && (val & 1))
    s->pit->ch[2].load_ns = now_ns();
  return 0;
}

void pit_init(struct pio_bus *bus) {
  struct pit *p;
  struct device *dev;
  struct port61 *p61;
  struct device *dev61;

  p = malloc(sizeof *p);
  if (!p)
    return;
  memset(p, 0, sizeof *p);

  u64 t = now_ns();
  for (int i = 0; i < NUM_CHANNELS; i++) {
    p->ch[i].access = ACCESS_LOHI;
    p->ch[i].mode = 2;
    p->ch[i].load_ns = t;
  }

  dev = new_device(pit_read, pit_write, p);
  if (!dev) {
    free(p);
    return;
  }
  pio_register(bus, PIT_PORT_BASE, PIT_PORT_END, dev);

  p61 = malloc(sizeof *p61);
  if (!p61)
    return;
  p61->pit = p;
  p61->val = 0;
  dev61 = new_device(port61_read, port61_write, p61);
  if (!dev61) {
    free(p61);
    return;
  }
  pio_register(bus, 0x61, 0x62, dev61);
}
