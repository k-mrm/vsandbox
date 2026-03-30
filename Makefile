CC      = gcc
CFLAGS  = -Wall -Og -Wextra -g -I.
LDFLAGS = -lpthread

SRCS = main.c vm.c vcpu.c loader.c hv/kvm.c hw/pio.c hw/mmio.c hw/device.c hw/legacy-serial.c hw/fw_cfg.c hw/ioapic.c hw/lapic.c hw/pit.c hw/cmos.c hw/sysctl.c hw/debugcon.c hw/pci.c hw/dma.c hw/pic.c hw/kbd.c hw/ata.c hw/lpt.c hw/vga.c
OBJS = $(SRCS:.c=.o)
TARGET = vsandbox

GUEST_CC     = gcc
GUEST_CFLAGS = -m32 -ffreestanding -fno-pic -O2
GUEST_LD     = ld
GUEST_LDFLAGS = -m elf_i386 -T guest.ld

all: $(TARGET) guest.bin boot/fwboot.bin

$(TARGET): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

boot/fwboot.bin: boot/fwboot.S boot/fwboot.ld
	$(GUEST_CC) -m32 -c -o boot/fwboot.o boot/fwboot.S
	$(GUEST_LD) -m elf_i386 -T boot/fwboot.ld -o $@ boot/fwboot.o
	rm -f boot/fwboot.o
	python3 -c "d=bytearray(open('$@','rb').read());d[-1]=(-sum(d[:-1]))&0xff;open('$@','wb').write(d)"

guest.bin: guest.o guest.ld
	$(GUEST_LD) $(GUEST_LDFLAGS) -o $@ guest.o

guest.o: guest.c
	$(GUEST_CC) $(GUEST_CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET) guest.o guest.bin boot/fwboot.o boot/fwboot.bin

.PHONY: all clean
