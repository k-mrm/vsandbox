#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "hw/device.h"
#include "hw/pio.h"

#define CMOS_PORT_BASE  0x70
#define CMOS_PORT_END   0x72
#define CMOS_SIZE       128

/* standard CMOS offsets */
#define CMOS_RTC_SEC    0x00
#define CMOS_RTC_MIN    0x02
#define CMOS_RTC_HOUR   0x04
#define CMOS_RTC_DOW    0x06
#define CMOS_RTC_DOM    0x07
#define CMOS_RTC_MON    0x08
#define CMOS_RTC_YEAR   0x09
#define CMOS_REG_A      0x0A
#define CMOS_REG_B      0x0B
#define CMOS_REG_C      0x0C
#define CMOS_REG_D      0x0D
#define CMOS_MEM_LO     0x15  /* base memory low byte (KB) */
#define CMOS_MEM_HI     0x16
#define CMOS_EXT_LO     0x17  /* extended memory low byte (KB) */
#define CMOS_EXT_HI     0x18
#define CMOS_EXT2_LO    0x30  /* same as 0x17/0x18 */
#define CMOS_EXT2_HI    0x31
#define CMOS_CENTURY    0x32
#define CMOS_BOOT_DEV   0x3D

static u8 to_bcd(int v) {
  return (u8)(((v / 10) << 4) | (v % 10));
}

struct cmos {
  u8 addr;
  u8 data[CMOS_SIZE];
};

static void cmos_update_rtc(struct cmos *c) {
  time_t t = time(NULL);
  struct tm *tm = gmtime(&t);
  c->data[CMOS_RTC_SEC]  = to_bcd(tm->tm_sec);
  c->data[CMOS_RTC_MIN]  = to_bcd(tm->tm_min);
  c->data[CMOS_RTC_HOUR] = to_bcd(tm->tm_hour);
  c->data[CMOS_RTC_DOW]  = to_bcd(tm->tm_wday + 1);
  c->data[CMOS_RTC_DOM]  = to_bcd(tm->tm_mday);
  c->data[CMOS_RTC_MON]  = to_bcd(tm->tm_mon + 1);
  c->data[CMOS_RTC_YEAR] = to_bcd(tm->tm_year % 100);
  c->data[CMOS_CENTURY]  = to_bcd((tm->tm_year + 1900) / 100);
}

static int cmos_read(struct device *dev, u32 offset, void *d, u32 size) {
  struct cmos *c = dev->priv;
  (void)size;
  if (offset == 0) {
    *(u8 *)d = c->addr;
  } else {
    if (c->addr <= CMOS_RTC_YEAR || c->addr == CMOS_CENTURY)
      cmos_update_rtc(c);
    *(u8 *)d = c->data[c->addr & 0x7f];
  }
  return 0;
}

static int cmos_write(struct device *dev, u32 offset, void *d, u32 size) {
  struct cmos *c = dev->priv;
  u8 val = *(u8 *)d;
  (void)size;
  if (offset == 0) {
    c->addr = val & 0x7f;
  } else {
    c->data[c->addr & 0x7f] = val;
  }
  return 0;
}

void cmos_init(struct pio_bus *bus, u32 mem_mb) {
  struct cmos *c;
  struct device *dev;
  u32 ext_kb;

  c = malloc(sizeof *c);
  if (!c)
    return;
  memset(c, 0, sizeof *c);

  /* status registers */
  c->data[CMOS_REG_A] = 0x26;  /* divider + rate */
  c->data[CMOS_REG_B] = 0x02;  /* 24h mode, BCD */
  c->data[CMOS_REG_C] = 0x00;
  c->data[CMOS_REG_D] = 0x80;  /* valid RAM / battery OK */

  /* base memory: 640 KB */
  c->data[CMOS_MEM_LO] = 0x80;
  c->data[CMOS_MEM_HI] = 0x02;

  /* extended memory (above 1MB), capped at 65535 KB */
  ext_kb = (mem_mb - 1) * 1024;
  if (ext_kb > 0xffff)
    ext_kb = 0xffff;
  c->data[CMOS_EXT_LO]  = ext_kb & 0xff;
  c->data[CMOS_EXT_HI]  = (ext_kb >> 8) & 0xff;
  c->data[CMOS_EXT2_LO] = ext_kb & 0xff;
  c->data[CMOS_EXT2_HI] = (ext_kb >> 8) & 0xff;

  c->data[CMOS_BOOT_DEV] = 0x02;  /* boot from first HD */

  dev = new_device(cmos_read, cmos_write, c);
  if (!dev) {
    free(c);
    return;
  }
  pio_register(bus, CMOS_PORT_BASE, CMOS_PORT_END, dev);
}
