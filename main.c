#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <termios.h>
#include <poll.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include "backend.h"
#include "loader.h"
#include "vm.h"

#define GUEST_MEM_SIZE (512 << 20)

enum mmap_type { MMAP_RAM, MMAP_BIOS };

struct phys_map {
  ulong base;
  ulong size;
  int type;
  void *hva;
};

static struct phys_map pmap[] = {
  { 0x00000000, 0x000C0000,                    MMAP_RAM,  NULL },  /* Low RAM */
  { 0x000C0000, 0x00040000,                    MMAP_BIOS, NULL },  /* BIOS shadow */
  { 0x00100000, GUEST_MEM_SIZE - 0x100000,     MMAP_RAM,  NULL },  /* Extended RAM */
  { 0xFFFC0000, 0x00040000,                    MMAP_BIOS, NULL },  /* BIOS ROM high */
};

#define NPMAP (sizeof(pmap) / sizeof(pmap[0]))

struct backend *B = NULL;

static struct termios orig_term;
static int term_saved;

static void sig_nop(int sig) { (void)sig; }

static void term_raw(void) {
  struct termios t;
  if (!isatty(STDIN_FILENO))
    return;
  tcgetattr(STDIN_FILENO, &orig_term);
  term_saved = 1;
  t = orig_term;
  t.c_lflag &= ~(ICANON | ECHO);
  t.c_cc[VMIN] = 0;
  t.c_cc[VTIME] = 0;
  tcsetattr(STDIN_FILENO, TCSANOW, &t);
}

static void term_restore(void) {
  if (term_saved)
    tcsetattr(STDIN_FILENO, TCSANOW, &orig_term);
}

void panic(const char *fmt) {
  term_restore();
  fprintf(stderr, "%s\n", fmt);
  exit(1);
}

static void *openguest(const char *path, size_t *sz) {
  struct stat st;
  int fd;
  ssize_t n;
  void *buf;

  fd = open(path, O_RDONLY);
  if (fd < 0)
    return NULL;
  if (fstat(fd, &st) < 0)
    return NULL;
  buf = malloc(st.st_size);
  if (!buf)
    return NULL;
  n = read(fd, buf, st.st_size);
  close(fd);
  if (n != st.st_size)
    return NULL;
  *sz = st.st_size;
  return buf;
}

struct opt {
  char *bpath;
  char *gpath;
};

void parseopt(int argc, char **argv, struct opt *p) {
  int opt;
  memset(p, 0, sizeof *p);
  while ((opt = getopt(argc, argv, "b:")) != -1) {
    switch (opt) {
    case 'b':
      p->bpath = optarg;
      break;
    default:
      panic("?");
    }
  }
  if (optind < argc)
    p->gpath = argv[optind];
}

int main(int argc, char **argv) {
  struct opt opt;
  struct vmconfig cfg = { .ncpu = 1, .memsz = GUEST_MEM_SIZE };
  struct vm *vm;
  void *mem, *bios, *rom_hi, *rom_lo;
  size_t bsz;

  parseopt(argc, argv, &opt);
  if (!opt.bpath || !opt.gpath)
    panic("usage: vsandbox -b bios.bin guest");

  /* read BIOS file */
  bios = openguest(opt.bpath, &bsz);
  if (!bios)
    panic("load bios?");

  B = kvm_backend();
  vm = create_vm(&cfg);

  /* guest RAM: one contiguous mmap */
  mem = mmap(NULL, GUEST_MEM_SIZE, PROT_READ | PROT_WRITE,
             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (mem == MAP_FAILED)
    panic("guest mem?");

  /* BIOS ROM buffers — fixed size regions, data aligned to end */
  rom_hi = mmap(NULL, pmap[3].size, PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (rom_hi == MAP_FAILED)
    panic("mmap rom_hi?");
  memcpy((char *)rom_hi + pmap[3].size - bsz, bios, bsz);

  rom_lo = mmap(NULL, pmap[1].size, PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (rom_lo == MAP_FAILED)
    panic("mmap rom_lo?");
  memcpy((char *)rom_lo + pmap[1].size - bsz, bios, bsz);

  free(bios);

  /* iterate phys_map and register each region */
  int bios_idx = 0;
  void *bios_bufs[] = { rom_lo, rom_hi };
  for (size_t i = 0; i < NPMAP; i++) {
    struct phys_map *p = &pmap[i];
    if (p->type == MMAP_RAM) {
      p->hva = (char *)mem + p->base;
    } else {
      p->hva = bios_bufs[bios_idx++];
    }
    if (vm_mapmem(vm, p->base, p->hva, p->size) < 0) {
      fprintf(stderr, "mapmem failed: base=0x%lx size=0x%lx\n",
              p->base, p->size);
      panic("mapmem?");
    }
  }

  /* load guest kernel into RAM and register via fw_cfg */
  {
    size_t gsz;
    void *gimg = openguest(opt.gpath, &gsz);
    if (!gimg)
      panic("load guest?");

    struct elfinfo ei;
    if (load_elf(gimg, gsz, mem, GUEST_MEM_SIZE, &ei) < 0)
      panic("load elf?");

    vm->kernel_data = gimg;
    vm->kernel_size = gsz;
    vm->kernel_addr = ei.entry;
    vm->kernel_entry = ei.entry;

    /* register option ROM so SeaBIOS boots via fw_cfg */
    size_t rsz;
    void *rom = openguest("boot/fwboot.bin", &rsz);
    if (!rom)
      panic("load fwboot.bin?");
    fw_cfg_add_file(vm->fw_cfg, "genroms/fwboot.bin", rom, rsz);
  }

  if (vcpu_setup_bios(vm->vcpu[0]) < 0)
    panic("vcpu?");

  signal(SIGUSR1, sig_nop);
  term_raw();

  for (int i = 0; i < vm->ncpu; i++)
    vcpu_kick(vm->vcpu[i]);

  /* monitor stdin for ctrl-a */
  for (;;) {
    struct pollfd pfd = { .fd = STDIN_FILENO, .events = POLLIN };
    int alive = 0;
    for (int i = 0; i < vm->ncpu; i++)
      if (vm->vcpu[i]->online)
        alive = 1;
    if (!alive)
      break;
    if (poll(&pfd, 1, 100) > 0 && (pfd.revents & POLLIN)) {
      char c;
      if (read(STDIN_FILENO, &c, 1) == 1) {
        if (c == '\x01') {
          for (int i = 0; i < vm->ncpu; i++) {
            vm->vcpu[i]->dump = 1;
            pthread_kill(vm->vcpu[i]->thread, SIGUSR1);
          }
        } else {
          kbd_push_key((u8)c);
        }
      }
    }
  }

  for (int i = 0; i < vm->ncpu; i++)
    vcpu_wait(vm->vcpu[i]);
  term_restore();
  return 0;
}
