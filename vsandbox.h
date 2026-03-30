#ifndef _VS_VSANDBOX_H
#define _VS_VSANDBOX_H

#include <stddef.h>

typedef unsigned long u64;
typedef long i64;
typedef unsigned int u32;
typedef signed int i32;
typedef unsigned short u16;
typedef signed short i16;
typedef unsigned char u8;
typedef signed char i8;
typedef unsigned int uint;
typedef unsigned short ushort;
typedef unsigned long ulong;
typedef unsigned char uchar;

#define container_of(ptr, st, m)                                               \
  ({                                                                           \
    const typeof(((st *)0)->m) *_mptr = (ptr);                                 \
    (st *)((char *)_mptr - offsetof(st, m));                                   \
  })

#define MAX(a, b) ((a) < (b) ? (b) : (a))
#define MIN(a, b) ((a) > (b) ? (b) : (a))

void panic(const char *fmt);

#endif  // _VS_VSANDBOX_H
