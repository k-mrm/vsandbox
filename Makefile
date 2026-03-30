CC      = gcc
CFLAGS  = -Wall -Og -Wextra -g -I.
LDFLAGS = -lpthread

SRCS = main.c vm.c vcpu.c loader.c kvm.c pio.c mmio.c device.c serial.c fw_cfg.c ioapic.c lapic.c pit.c cmos.c sysctl.c pci.c dma.c pic.c kbd.c ata.c lpt.c vga.c
OBJS = $(SRCS:.c=.o)
TARGET = vsandbox

GUEST_CC     = gcc
GUEST_CFLAGS = -m32 -ffreestanding -fno-pic -O2
GUEST_LD     = ld
GUEST_LDFLAGS = -m elf_i386 -T guest.ld

all: $(TARGET) boot/fwboot.bin

$(TARGET): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

boot/fwboot.bin: boot/fwboot.S boot/fwboot.ld
	$(GUEST_CC) -m32 -c -o boot/fwboot.o boot/fwboot.S
	$(GUEST_LD) -m elf_i386 -T boot/fwboot.ld -o $@ boot/fwboot.o
	rm -f boot/fwboot.o
	python3 -c "d=bytearray(open('$@','rb').read());d[-1]=(-sum(d[:-1]))&0xff;open('$@','wb').write(d)"

clean:
	rm -f $(OBJS) $(TARGET) boot/fwboot.o boot/fwboot.bin

.PHONY: all clean
