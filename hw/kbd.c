#include <stdlib.h>
#include <string.h>
#include "hw/device.h"
#include "hw/pio.h"
#include "vm.h"

#define KBD_DATA_PORT  0x60
#define KBD_CMD_PORT   0x64

/* status bits */
#define KBD_STAT_OBF   0x01  /* output buffer full */
#define KBD_STAT_IBF   0x02  /* input buffer full */
#define KBD_STAT_SYS   0x04  /* system flag */

/* controller commands */
#define KBD_CMD_READ_CONFIG   0x20
#define KBD_CMD_WRITE_CONFIG  0x60
#define KBD_CMD_DISABLE_AUX   0xA7
#define KBD_CMD_ENABLE_AUX    0xA8
#define KBD_CMD_TEST_AUX      0xA9
#define KBD_CMD_SELF_TEST     0xAA
#define KBD_CMD_TEST_PORT     0xAB
#define KBD_CMD_DISABLE_KBD   0xAD
#define KBD_CMD_ENABLE_KBD    0xAE
#define KBD_CMD_WRITE_AUX     0xD4

#define KBD_QUEUE_SIZE 128
#define LSHIFT_MAKE    0x2a
#define LSHIFT_BREAK   0xaa
#define LCTRL_MAKE     0x1d
#define LCTRL_BREAK    0x9d

struct kbd {
  u8 status;
  u8 config;
  u8 pending_cmd;
  u8 queue[KBD_QUEUE_SIZE];
  int head, tail;
};

static struct kbd *the_kbd;

/* scan code set 1 make codes indexed by ASCII */
static const u8 scancode[128] = {
  [0x08] = 0x0e, /* BS */
  [0x09] = 0x0f, /* TAB */
  [0x0a] = 0x1c, /* LF → Enter */
  [0x0d] = 0x1c, /* CR → Enter */
  [0x1b] = 0x01, /* ESC */
  [0x7f] = 0x0e, /* DEL → Backspace */
  [' ']  = 0x39,
  ['0']  = 0x0b, ['1'] = 0x02, ['2'] = 0x03, ['3'] = 0x04,
  ['4']  = 0x05, ['5'] = 0x06, ['6'] = 0x07, ['7'] = 0x08,
  ['8']  = 0x09, ['9'] = 0x0a,
  ['a']  = 0x1e, ['b'] = 0x30, ['c'] = 0x2e, ['d'] = 0x20,
  ['e']  = 0x12, ['f'] = 0x21, ['g'] = 0x22, ['h'] = 0x23,
  ['i']  = 0x17, ['j'] = 0x24, ['k'] = 0x25, ['l'] = 0x26,
  ['m']  = 0x32, ['n'] = 0x31, ['o'] = 0x18, ['p'] = 0x19,
  ['q']  = 0x10, ['r'] = 0x13, ['s'] = 0x1f, ['t'] = 0x14,
  ['u']  = 0x16, ['v'] = 0x2f, ['w'] = 0x11, ['x'] = 0x2d,
  ['y']  = 0x15, ['z'] = 0x2c,
  ['-']  = 0x0c, ['='] = 0x0d,
  ['[']  = 0x1a, [']'] = 0x1b,
  [';']  = 0x27, ['\''] = 0x28,
  ['`']  = 0x29, ['\\'] = 0x2b,
  [',']  = 0x33, ['.'] = 0x34, ['/'] = 0x35,
};

/* shifted chars → base scan code */
static const u8 shift_scancode[128] = {
  ['A']  = 0x1e, ['B'] = 0x30, ['C'] = 0x2e, ['D'] = 0x20,
  ['E']  = 0x12, ['F'] = 0x21, ['G'] = 0x22, ['H'] = 0x23,
  ['I']  = 0x17, ['J'] = 0x24, ['K'] = 0x25, ['L'] = 0x26,
  ['M']  = 0x32, ['N'] = 0x31, ['O'] = 0x18, ['P'] = 0x19,
  ['Q']  = 0x10, ['R'] = 0x13, ['S'] = 0x1f, ['T'] = 0x14,
  ['U']  = 0x16, ['V'] = 0x2f, ['W'] = 0x11, ['X'] = 0x2d,
  ['Y']  = 0x15, ['Z'] = 0x2c,
  ['!']  = 0x02, ['@'] = 0x03, ['#'] = 0x04, ['$'] = 0x05,
  ['%']  = 0x06, ['^'] = 0x07, ['&'] = 0x08, ['*'] = 0x09,
  ['(']  = 0x0a, [')'] = 0x0b,
  ['_']  = 0x0c, ['+'] = 0x0d,
  ['{']  = 0x1a, ['}'] = 0x1b,
  [':']  = 0x27, ['"'] = 0x28,
  ['~']  = 0x29, ['|'] = 0x2b,
  ['<']  = 0x33, ['>'] = 0x34, ['?'] = 0x35,
};

static int kbd_queue_empty(struct kbd *k) {
  return k->head == k->tail;
}

static void kbd_enqueue(struct kbd *k, u8 val) {
  int next = (k->tail + 1) % KBD_QUEUE_SIZE;
  if (next == k->head)
    return;  /* full, drop */
  k->queue[k->tail] = val;
  k->tail = next;
  k->status |= KBD_STAT_OBF;
}

static u8 kbd_dequeue(struct kbd *k) {
  u8 val;
  if (kbd_queue_empty(k))
    return 0;
  val = k->queue[k->head];
  k->head = (k->head + 1) % KBD_QUEUE_SIZE;
  if (kbd_queue_empty(k))
    k->status &= ~KBD_STAT_OBF;
  return val;
}

void kbd_push_key(u8 ascii) {
  struct kbd *k = the_kbd;
  u8 code;
  int shifted;

  if (!k)
    return;

  code = 0;
  shifted = 0;

  /* ctrl+letter: 0x01-0x1a → LCTRL + 'a'-'z' */
  if (ascii >= 0x01 && ascii <= 0x1a && ascii != '\t' && ascii != '\n' && ascii != '\r') {
    code = scancode['a' + ascii - 1];
    if (code) {
      kbd_enqueue(k, LCTRL_MAKE);
      kbd_enqueue(k, code);
      kbd_enqueue(k, code | 0x80);
      kbd_enqueue(k, LCTRL_BREAK);
      goto out;
    }
  }

  if (ascii < 128 && scancode[ascii]) {
    code = scancode[ascii];
  } else if (ascii < 128 && shift_scancode[ascii]) {
    code = shift_scancode[ascii];
    shifted = 1;
  }

  if (!code)
    return;

  if (shifted)
    kbd_enqueue(k, LSHIFT_MAKE);
  kbd_enqueue(k, code);          /* make */
  kbd_enqueue(k, code | 0x80);   /* break */
  if (shifted)
    kbd_enqueue(k, LSHIFT_BREAK);
out:

  ioapic_raise_irq(1);
}

static int kbd_data_read(struct device *dev, u32 offset, void *d, u32 size) {
  struct kbd *k = dev->priv;
  (void)offset; (void)size;
  *(u8 *)d = kbd_dequeue(k);
  return 0;
}

static int kbd_cmd_read(struct device *dev, u32 offset, void *d, u32 size) {
  struct kbd *k = dev->priv;
  (void)offset; (void)size;
  *(u8 *)d = k->status;
  return 0;
}

static int kbd_data_write(struct device *dev, u32 offset, void *d, u32 size) {
  struct kbd *k = dev->priv;
  u8 val = *(u8 *)d;
  (void)offset; (void)size;
  if (k->pending_cmd == KBD_CMD_WRITE_CONFIG) {
    k->config = val;
    k->pending_cmd = 0;
  } else if (k->pending_cmd == KBD_CMD_WRITE_AUX) {
    /* data byte forwarded to AUX (mouse): ACK from mouse */
    kbd_enqueue(k, 0xFA);
    k->pending_cmd = 0;
  } else {
    /* keyboard command */
    kbd_enqueue(k, 0xFA);  /* ACK */
    switch (val) {
    case 0xFF:  /* reset */
      kbd_enqueue(k, 0xAA);  /* self-test passed */
      break;
    case 0xF2:  /* identify */
      kbd_enqueue(k, 0xAB);
      kbd_enqueue(k, 0x83);
      break;
    }
  }
  return 0;
}

static int kbd_cmd_write(struct device *dev, u32 offset, void *d, u32 size) {
  struct kbd *k = dev->priv;
  u8 val = *(u8 *)d;
  (void)offset; (void)size;
  switch (val) {
  case KBD_CMD_READ_CONFIG:
    kbd_enqueue(k, k->config);
    break;
  case KBD_CMD_WRITE_CONFIG:
    k->pending_cmd = val;
    break;
  case KBD_CMD_SELF_TEST:
    kbd_enqueue(k, 0x55);
    break;
  case KBD_CMD_TEST_PORT:
    kbd_enqueue(k, 0x00);
    break;
  case KBD_CMD_TEST_AUX:
    kbd_enqueue(k, 0x00);
    break;
  case KBD_CMD_DISABLE_KBD:
    k->config |= 0x10;
    break;
  case KBD_CMD_ENABLE_KBD:
    k->config &= ~0x10;
    break;
  case KBD_CMD_DISABLE_AUX:
    k->config |= 0x20;
    break;
  case KBD_CMD_ENABLE_AUX:
    k->config &= ~0x20;
    break;
  case KBD_CMD_WRITE_AUX:
    k->pending_cmd = val;
    break;
  }
  return 0;
}

void kbd_init(struct pio_bus *bus) {
  struct kbd *k;
  struct device *data_dev, *cmd_dev;

  k = malloc(sizeof *k);
  if (!k)
    return;
  memset(k, 0, sizeof *k);
  k->status = KBD_STAT_SYS;
  k->config = 0x65;

  the_kbd = k;

  data_dev = new_device(kbd_data_read, kbd_data_write, k);
  if (!data_dev) {
    free(k);
    return;
  }
  cmd_dev = new_device(kbd_cmd_read, kbd_cmd_write, k);
  if (!cmd_dev) {
    free(data_dev);
    free(k);
    return;
  }
  pio_register(bus, KBD_DATA_PORT, KBD_DATA_PORT + 1, data_dev);
  pio_register(bus, KBD_CMD_PORT, KBD_CMD_PORT + 1, cmd_dev);
}
