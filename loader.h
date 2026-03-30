#ifndef _VS_LOADER_H
#define _VS_LOADER_H

#include <stddef.h>
#include "vsandbox.h"

struct elfinfo {
  ulong entry;
};

int load_elf(void *img, size_t imgsz, void *mem, ulong memsz,
             struct elfinfo *info);

#endif  // _VS_LOADER_H
