#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <arpa/inet.h>
#include "vm.h"
#include "device.h"
#include "pio.h"

/* fw_cfg selector keys */
#define FW_CFG_SIGNATURE    0x0000
#define FW_CFG_ID           0x0001
#define FW_CFG_UUID         0x0002
#define FW_CFG_RAM_SIZE     0x0003
#define FW_CFG_NOGRAPHIC    0x0004
#define FW_CFG_NB_CPUS      0x0005
#define FW_CFG_MACHINE_ID   0x0006
#define FW_CFG_KERNEL_ADDR  0x0007
#define FW_CFG_KERNEL_SIZE  0x0008
#define FW_CFG_KERNEL_CMDLINE 0x0009
#define FW_CFG_INITRD_ADDR  0x000a
#define FW_CFG_INITRD_SIZE  0x000b
#define FW_CFG_BOOT_DEVICE  0x000c
#define FW_CFG_NUMA         0x000d
#define FW_CFG_BOOT_MENU    0x000e
#define FW_CFG_MAX_CPUS     0x000f
#define FW_CFG_NB_NODES     0x000d
#define FW_CFG_KERNEL_ENTRY 0x0010
#define FW_CFG_KERNEL_DATA  0x0011
#define FW_CFG_SETUP_ADDR   0x0012
#define FW_CFG_SETUP_SIZE   0x0013
#define FW_CFG_SETUP_DATA   0x0014
#define FW_CFG_FILE_DIR     0x0019
/* I/O port offsets (base=0x510) */
#define FW_CFG_OFF_SEL      0   /* 0x510: selector (write, 16-bit) */
#define FW_CFG_OFF_DATA     1   /* 0x511: data (read, 8-bit) */

#define FW_CFG_MAX_FILES    16
#define FW_CFG_FILE_FIRST   0x0020

/* on-wire file directory entry (big-endian) */
struct fw_cfg_file_entry {
  u32 size;
  u16 select;
  u16 reserved;
  char name[56];
};

struct fw_cfg_item {
  void *data;
  u32 len;
};

struct fw_cfg {
  struct vm *vm;
  u16 sel;
  u32 off;
  u8 signature[4];
  u32 id;
  u8 boot_device;
  u16 boot_menu;
  u8 nographic;
  struct fw_cfg_file_entry files[FW_CFG_MAX_FILES];
  int nfiles;
  struct fw_cfg_item file_data[FW_CFG_MAX_FILES];
  void *dir_blob;
  u32 dir_blob_len;
};

static void cfgcopy(void *data, u32 off, void *c, u32 len, u32 limit) {
  for (u32 i = 0; i < len && off + i < limit; i++) {
    *(u8*)(data + i) = *(u8*)(c + off + i);
  }
}

static void getcfg(struct fw_cfg *cfg, void *data, u32 len) {
  struct vm *vm = cfg->vm;
  int idx;
  u64 ram_size;
  u16 nb_cpus, max_cpus;

  memset(data, 0, len);
  switch (cfg->sel) {
  case FW_CFG_SIGNATURE:
    cfgcopy(data, cfg->off, cfg->signature, len, sizeof cfg->signature);
    break;
  case FW_CFG_ID:
    cfgcopy(data, cfg->off, &cfg->id, len, sizeof cfg->id);
    break;
  case FW_CFG_RAM_SIZE:
    ram_size = vm->memsz;
    cfgcopy(data, cfg->off, &ram_size, len, sizeof ram_size);
    break;
  case FW_CFG_NB_CPUS:
    nb_cpus = vm->ncpu;
    cfgcopy(data, cfg->off, &nb_cpus, len, sizeof nb_cpus);
    break;
  case FW_CFG_MAX_CPUS:
    max_cpus = vm->ncpu;
    cfgcopy(data, cfg->off, &max_cpus, len, sizeof max_cpus);
    break;
  case FW_CFG_BOOT_DEVICE:
    cfgcopy(data, cfg->off, &cfg->boot_device, len, sizeof cfg->boot_device);
    break;
  case FW_CFG_BOOT_MENU:
    cfgcopy(data, cfg->off, &cfg->boot_menu, len, sizeof cfg->boot_menu);
    break;
  case FW_CFG_NOGRAPHIC:
    cfgcopy(data, cfg->off, &cfg->nographic, len, sizeof cfg->nographic);
    break;
  case FW_CFG_KERNEL_ADDR:
    cfgcopy(data, cfg->off, &vm->kernel_addr, len, sizeof vm->kernel_addr);
    break;
  case FW_CFG_KERNEL_SIZE:
    cfgcopy(data, cfg->off, &vm->kernel_size, len, sizeof vm->kernel_size);
    break;
  case FW_CFG_KERNEL_CMDLINE:
    if (vm->kernel_cmdline)
      cfgcopy(data, cfg->off, vm->kernel_cmdline, len, strlen(vm->kernel_cmdline) + 1);
    break;
  case FW_CFG_KERNEL_ENTRY:
    cfgcopy(data, cfg->off, &vm->kernel_entry, len, sizeof vm->kernel_entry);
    break;
  case FW_CFG_KERNEL_DATA:
    if (vm->kernel_data)
      cfgcopy(data, cfg->off, vm->kernel_data, len, vm->kernel_size);
    break;
  case FW_CFG_SETUP_SIZE: {
    u32 zero = 0;
    cfgcopy(data, cfg->off, &zero, len, sizeof zero);
    break;
  }
  case FW_CFG_FILE_DIR:
    cfgcopy(data, cfg->off, cfg->dir_blob, len, cfg->dir_blob_len);
    break;
  default:
    if (cfg->sel >= FW_CFG_FILE_FIRST &&
        cfg->sel < FW_CFG_FILE_FIRST + cfg->nfiles) {
      idx = cfg->sel - FW_CFG_FILE_FIRST;
      cfgcopy(data, cfg->off, cfg->file_data[idx].data, len, cfg->file_data[idx].len);
    }
    break;
  }
}

static int fw_cfg_read(struct device *dev, u32 offset, void *d, u32 size) {
  struct fw_cfg *cfg = dev->priv;

  if (offset == FW_CFG_OFF_DATA) {
    getcfg(cfg, d, size);
    cfg->off += size;
  } else {
    *(u8 *)d = 0;
  }
  return 0;
}

static int fw_cfg_write(struct device *dev, u32 offset, void *d, u32 size) {
  struct fw_cfg *cfg = dev->priv;

  if (offset == FW_CFG_OFF_SEL && size == 2) {
    cfg->sel = *(u16 *)d;
    cfg->off = 0;
  }
  return 0;
}

static void fw_cfg_build_dir(struct fw_cfg *cfg) {
  u32 count = htonl(cfg->nfiles);
  u32 blob_len = 4 + cfg->nfiles * sizeof(struct fw_cfg_file_entry);

  cfg->dir_blob = malloc(blob_len);
  if (!cfg->dir_blob) {
    cfg->dir_blob_len = 0;
    return;
  }
  memcpy(cfg->dir_blob, &count, 4);
  memcpy((char *)cfg->dir_blob + 4, cfg->files, cfg->nfiles * sizeof(struct fw_cfg_file_entry));
  cfg->dir_blob_len = blob_len;
}

int fw_cfg_add_file(struct fw_cfg *cfg, const char *name, void *data, u32 len) {
  struct fw_cfg_file_entry *fe;
  int idx = cfg->nfiles;
  u16 sel = FW_CFG_FILE_FIRST + idx;
  if (cfg->nfiles >= FW_CFG_MAX_FILES)
    return -1;
  fe = &cfg->files[idx];
  memset(fe, 0, sizeof *fe);
  fe->size = htonl(len);
  fe->select = htons(sel);
  strncpy(fe->name, name, sizeof(fe->name) - 1);
  cfg->file_data[idx].data = data;
  cfg->file_data[idx].len = len;
  cfg->nfiles++;
  free(cfg->dir_blob);
  fw_cfg_build_dir(cfg);
  return 0;
}

struct fw_cfg *fw_cfg_init(struct vm *vm) {
  struct device *dev;
  struct fw_cfg *cfg;

  cfg = malloc(sizeof *cfg);
  if (!cfg)
    return NULL;
  memset(cfg, 0, sizeof *cfg);
  cfg->vm = vm;
  memcpy(cfg->signature, "QEMU", 4);
  cfg->id = 1;
  cfg->boot_device = 'c';
  fw_cfg_build_dir(cfg);
  dev = new_device(fw_cfg_read, fw_cfg_write, cfg);
  if (!dev) {
    free(cfg);
    return NULL;
  }
  pio_register(vm->pio, 0x510, 0x512, dev);
  return cfg;
}
