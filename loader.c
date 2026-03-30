#include <string.h>
#include <stdio.h>
#include "elf.h"
#include "loader.h"

int load_elf(void *img, size_t imgsz, void *mem, ulong memsz,
             struct elfinfo *info) {
  Elf32_Ehdr *ehdr = img;
  Elf32_Off phoff;
  Elf32_Half phnum, phentsize;

  if (imgsz < sizeof(Elf32_Ehdr))
    return -1;
  if (memcmp(ehdr->e_ident, ELFMAG, SELFMAG) != 0) {
    fprintf(stderr, "elf: bad magic\n");
    return -1;
  }
  if (ehdr->e_ident[4] != ELFCLASS32) {
    fprintf(stderr, "elf: not ELF32\n");
    return -1;
  }
  if (ehdr->e_machine != EM_386) {
    fprintf(stderr, "elf: not i386\n");
    return -1;
  }
  phoff = ehdr->e_phoff;
  phnum = ehdr->e_phnum;
  phentsize = ehdr->e_phentsize;
  if (phoff + (size_t)phnum * phentsize > imgsz) {
    fprintf(stderr, "elf: program headers out of bounds\n");
    return -1;
  }
  for (Elf32_Half i = 0; i < phnum; i++) {
    Elf32_Phdr *phdr = (Elf32_Phdr *)((char *)img + phoff + i * phentsize);
    if (phdr->p_type != PT_LOAD)
      continue;
    if ((ulong)phdr->p_paddr + phdr->p_memsz > memsz) {
      fprintf(stderr, "elf: segment %u at paddr 0x%x + 0x%x exceeds guest memory\n", i, phdr->p_paddr, phdr->p_memsz);
      return -1;
    }
    if ((size_t)phdr->p_offset + phdr->p_filesz > imgsz) {
      fprintf(stderr, "elf: segment %u file data out of bounds\n", i);
      return -1;
    }
    memcpy((char *)mem + phdr->p_paddr, (char *)img + phdr->p_offset,
           phdr->p_filesz);
  }
  info->entry = ehdr->e_entry;
  return 0;
}
