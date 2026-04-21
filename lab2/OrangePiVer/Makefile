RISCV_GNU ?= riscv64-unknown-elf
CC       = $(RISCV_GNU)-gcc
LD       = $(RISCV_GNU)-ld
OBJCOPY  = $(RISCV_GNU)-objcopy

CFLAGS   = -mcmodel=medany -ffreestanding -nostdlib -g -Wall -c

BOOTLOADER = kernel
TARGET2    = loader_target

all: build

build: clean initramfs.cpio
	$(CC) $(CFLAGS) *.S *.c
	$(LD) -T link.ld -o $(BOOTLOADER).elf *.o
	$(OBJCOPY) -O binary $(BOOTLOADER).elf $(BOOTLOADER).bin
	mkimage -f kernel.its $(BOOTLOADER).fit

	$(LD) -T link_loader_target.ld -o $(TARGET2).elf *.o
	$(OBJCOPY) -O binary $(TARGET2).elf $(TARGET2).bin

initramfs.cpio:
	cd rootfs && find . | cpio -o -H newc > ../initramfs.cpio

clean:
	rm -f *.o *.elf *.bin *.fit initramfs.cpio