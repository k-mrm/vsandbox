#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <pthread.h>
#include "vm.h"
#include "vcpu.h"
#include "device.h"
#include "mmio.h"
#include "backend.h"

#define LAPIC_BASE        0xFEE00000UL
#define LAPIC_SIZE        0x1000

/* register offsets */
#define REG_ID            0x020
#define REG_VER           0x030
#define REG_TPR           0x080
#define REG_APR           0x090
#define REG_PPR           0x0A0
#define REG_EOI           0x0B0
#define REG_LDR           0x0D0
#define REG_DFR           0x0E0
#define REG_SVR           0x0F0
#define REG_ISR_BASE      0x100  /* 0x100-0x170, 8 regs */
#define REG_TMR_BASE      0x180  /* 0x180-0x1F0, 8 regs */
#define REG_IRR_BASE      0x200  /* 0x200-0x270, 8 regs */
#define REG_ESR           0x280
#define REG_ICR_LO        0x300
#define REG_ICR_HI        0x310
#define REG_LVT_TIMER     0x320
#define REG_LVT_THERMAL   0x330
#define REG_LVT_PERF      0x340
#define REG_LVT_LINT0     0x350
#define REG_LVT_LINT1     0x360
#define REG_LVT_ERR       0x370
#define REG_TIMER_ICR     0x380
#define REG_TIMER_CCR     0x390
#define REG_TIMER_DCR     0x3E0

/* SVR bits */
#define SVR_APIC_EN       (1u << 8)

/* LVT bits */
#define LVT_MASKED        (1u << 16)
#define LVT_VECTOR(v)     ((v) & 0xff)
#define LVT_DELMODE(v)    (((v) >> 8) & 7)
#define LVT_TIMER_MODE(v) (((v) >> 17) & 3)
#define TIMER_ONESHOT     0
#define TIMER_PERIODIC    1
#define TIMER_TSC_DL      2

/* version: APIC integrated, version 0x14, max LVT entry 5 (0-indexed) */
#define LAPIC_VERSION     ((5 << 16) | 0x14)

struct lapic {
  u32 id;
  u32 tpr;
  u32 ldr;
  u32 dfr;
  u32 svr;
  u32 esr;
  u32 icr_lo;
  u32 icr_hi;
  u32 lvt_timer;
  u32 lvt_thermal;
  u32 lvt_perf;
  u32 lvt_lint0;
  u32 lvt_lint1;
  u32 lvt_err;
  u32 timer_icr;
  u32 timer_dcr;
  u32 timer_div;
  u32 isr[8];
  u32 irr[8];
  u32 tmr[8];
  /* timer state */
  u64 timerstart;
  int armed;
};

struct lapic_dev {
  struct lapic cpu[16];
};

static struct lapic_dev *lapicdev;

static u64 timernow(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (u64)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

static void bmp_set(u32 bmp[8], int vec) {
  bmp[vec >> 5] |= (1u << (vec & 31));
}

static void bmp_clear(u32 bmp[8], int vec) {
  bmp[vec >> 5] &= ~(1u << (vec & 31));
}

static int bmp_test(u32 bmp[8], int vec) {
  return (bmp[vec >> 5] >> (vec & 31)) & 1;
}

static int bmp_highest(u32 bmp[8]) {
  for (int i = 7; i >= 0; i--) {
    if (!bmp[i])
      continue;
    return (i << 5) | (31 - __builtin_clz(bmp[i]));
  }
  return -1;
}

static u32 ppr_get(struct lapic *s) {
  int isrv = bmp_highest(s->isr);
  u32 isrc = (isrv >= 0) ? ((u32)isrv & 0xf0) : 0;
  u32 tprc = s->tpr & 0xf0;
  if (tprc >= isrc)
    return s->tpr;
  return isrc | (isrv & 0x0f);
}

static u32 divider_value(u32 dcr) {
  u32 v = ((dcr >> 1) & 4) | (dcr & 3);
  if (v == 7)
    return 1;
  return 2u << v;
}

static int lapicpending(struct lapic *s) {
  int vec;
  u32 ppr;
  if (!(s->svr & SVR_APIC_EN))
    return -1;
  vec = bmp_highest(s->irr);
  if (vec < 0)
    return -1;
  ppr = ppr_get(s);
  if ((u32)(vec & 0xf0) <= (ppr & 0xf0))
    return -1;
  return vec;
}

static void raise(struct lapic *s, int vec) {
  if (B->inject_irq(current, vec) == 0) {
    bmp_clear(s->irr, vec);
    bmp_set(s->isr, vec);
  }
}

static void lapicirq(struct lapic *s, int vec) {
  bmp_set(s->irr, vec);
}

void lapic_send_irq(int cpu, int vec) {
  if (!lapicdev || cpu < 0 || cpu >= 16 || vec < 16 || vec > 255)
    return;
  __sync_fetch_and_or(&lapicdev->cpu[cpu].irr[vec >> 5], 1u << (vec & 31));
}

static int lapictimerirq(struct lapic *s) {
  int vec = LVT_VECTOR(s->lvt_timer);
  if (vec < 16 || vec > 255)
    return -1;
  lapicirq(s, vec);
  return 0;
}

static void lapiceoi(struct lapic *s) {
  int vec = bmp_highest(s->isr);
  if (vec < 0)
    return;
  bmp_clear(s->isr, vec);
  (void)bmp_test(s->tmr, vec);
}

static void timerreset(struct lapic *s, u32 icr) {
  s->timer_icr = icr;
  s->timer_div = divider_value(s->timer_dcr);
  if (icr == 0) {
    s->armed = 0;
    return;
  }
  s->timerstart = timernow();
  s->armed = 1;
}

#define LAPIC_BUS_PERIOD_NS  10  /* 100MHz: 1 tick = 10ns */

static u64 timerperiod(struct lapic *s) {
  return (u64)s->timer_icr * s->timer_div * LAPIC_BUS_PERIOD_NS;
}

static int timerirqmasked(struct lapic *s) {
  return s->lvt_timer & LVT_MASKED;
}

static int lapictimermode(struct lapic *s) {
  return LVT_TIMER_MODE(s->lvt_timer);
}

static void timercheck(struct lapic *s) {
  u64 elapsed, now;
  if (!s->armed)
    return;
  elapsed = timernow() - s->timerstart;
  if (elapsed < timerperiod(s))
    return;
  if (!timerirqmasked(s))
    lapictimerirq(s);
  if (lapictimermode(s) == TIMER_PERIODIC) {
    s->timerstart += timerperiod(s);
    now = timernow();
    if (now - s->timerstart >= timerperiod(s))
      s->timerstart = now;
  } else {
    s->armed = 0;
  }
}

static u32 timer_ccr_read(struct lapic *s) {
  if (!s->armed || s->timer_icr == 0)
    return 0;
  u64 period = timerperiod(s);
  u64 elapsed = timernow() - s->timerstart;
  if (elapsed >= period)
    return 0;
  u64 remaining_ticks = (period - elapsed) / (s->timer_div * LAPIC_BUS_PERIOD_NS);
  return (u32)remaining_ticks;
}

void lapictimer_tick(void) {
  struct lapic *s = &lapicdev->cpu[current->id];
  int vec;
  timercheck(s);
  vec = lapicpending(s);
  if (vec >= 0)
    raise(s, vec);
}

static int lapic_read(struct device *dev, u32 offset, void *d, u32 size) {
  struct lapic_dev *ld = dev->priv;
  struct lapic *s = &ld->cpu[current->id];
  u32 val = 0;
  (void)size;

  if (offset >= REG_ISR_BASE && offset < REG_ISR_BASE + 0x80) {
    val = s->isr[(offset - REG_ISR_BASE) >> 4];
    goto out;
  }
  if (offset >= REG_TMR_BASE && offset < REG_TMR_BASE + 0x80) {
    val = s->tmr[(offset - REG_TMR_BASE) >> 4];
    goto out;
  }
  if (offset >= REG_IRR_BASE && offset < REG_IRR_BASE + 0x80) {
    val = s->irr[(offset - REG_IRR_BASE) >> 4];
    goto out;
  }

  switch (offset) {
  case REG_ID:         val = s->id;          break;
  case REG_VER:        val = LAPIC_VERSION;  break;
  case REG_TPR:        val = s->tpr;         break;
  case REG_APR:        val = 0;              break;
  case REG_PPR:        val = ppr_get(s);     break;
  case REG_LDR:        val = s->ldr;         break;
  case REG_DFR:        val = s->dfr;         break;
  case REG_SVR:        val = s->svr;         break;
  case REG_ESR:        val = s->esr;         break;
  case REG_ICR_LO:     val = s->icr_lo;      break;
  case REG_ICR_HI:     val = s->icr_hi;      break;
  case REG_LVT_TIMER:  val = s->lvt_timer;   break;
  case REG_LVT_THERMAL:val = s->lvt_thermal; break;
  case REG_LVT_PERF:   val = s->lvt_perf;    break;
  case REG_LVT_LINT0:  val = s->lvt_lint0;   break;
  case REG_LVT_LINT1:  val = s->lvt_lint1;   break;
  case REG_LVT_ERR:    val = s->lvt_err;     break;
  case REG_TIMER_ICR:  val = s->timer_icr;   break;
  case REG_TIMER_CCR:  val = timer_ccr_read(s); break;
  case REG_TIMER_DCR:  val = s->timer_dcr;   break;
  }

out:
  *(u32 *)d = val;
  return 0;
}

static int lapic_write(struct device *dev, u32 offset, void *d, u32 size) {
  struct lapic_dev *ld = dev->priv;
  struct lapic *s = &ld->cpu[current->id];
  u32 val = *(u32 *)d;
  (void)size;
  switch (offset) {
  case REG_ID:
    s->id = val & 0xff000000;
    break;
  case REG_TPR:
    s->tpr = val & 0xff;
    break;
  case REG_EOI:
    lapiceoi(s);
    break;
  case REG_LDR:
    s->ldr = val & 0xff000000;
    break;
  case REG_DFR:
    s->dfr = val | 0x0fffffff;
    break;
  case REG_SVR:
    s->svr = val & 0x1ff;
    break;
  case REG_ESR:
    s->esr = 0;
    break;
  case REG_ICR_LO:
    s->icr_lo = val;
    break;
  case REG_ICR_HI:
    s->icr_hi = val;
    break;
  case REG_LVT_TIMER:
    s->lvt_timer = val;
    break;
  case REG_LVT_THERMAL:
    s->lvt_thermal = val;
    break;
  case REG_LVT_PERF:
    s->lvt_perf = val;
    break;
  case REG_LVT_LINT0:
    s->lvt_lint0 = val;
    break;
  case REG_LVT_LINT1:
    s->lvt_lint1 = val;
    break;
  case REG_LVT_ERR:
    s->lvt_err = val;
    break;
  case REG_TIMER_ICR:
    timerreset(s, val);
    break;
  case REG_TIMER_DCR:
    s->timer_dcr = val & 0xb;
    s->timer_div = divider_value(s->timer_dcr);
    break;
  }
  return 0;
}

void lapic_init(struct mmio_bus *bus) {
  struct lapic_dev *ld;
  struct device *dev;
  ld = malloc(sizeof *ld);
  if (!ld)
    return;
  memset(ld, 0, sizeof *ld);
  for (int i = 0; i < 16; i++) {
    struct lapic *s = &ld->cpu[i];
    s->id = (u32)i << 24;
    s->dfr = 0xffffffff;
    s->svr = 0xff;
    s->timer_div = 2;
    s->lvt_timer   = LVT_MASKED;
    s->lvt_thermal = LVT_MASKED;
    s->lvt_perf    = LVT_MASKED;
    s->lvt_lint0   = LVT_MASKED;
    s->lvt_lint1   = LVT_MASKED;
    s->lvt_err     = LVT_MASKED;
  }
  lapicdev = ld;
  dev = new_device(lapic_read, lapic_write, ld);
  if (!dev)
    panic("lapic?");
  mmio_register(bus, LAPIC_BASE, LAPIC_BASE + LAPIC_SIZE, dev);
}
