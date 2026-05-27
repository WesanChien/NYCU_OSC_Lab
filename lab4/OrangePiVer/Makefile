RISCV_GNU ?= riscv64-unknown-elf

CC       = $(RISCV_GNU)-gcc
LD       = $(RISCV_GNU)-ld
OBJCOPY  = $(RISCV_GNU)-objcopy

BOOTLOADER = kernel
TARGET2    = loader_target

CFLAGS = -mcmodel=medany -ffreestanding -nostdlib -g -Wall -IInc

C_SRCS = $(wildcard Src/*.c)
S_SRCS = $(wildcard *.S)

C_OBJS = $(notdir $(C_SRCS:.c=.o))
S_OBJS = $(notdir $(S_SRCS:.S=.o))

OBJS = $(S_OBJS) $(C_OBJS)

all: build

build: clean initramfs.cpio $(OBJS)
	$(LD) -T link.ld -o $(BOOTLOADER).elf $(OBJS)
	$(OBJCOPY) -O binary $(BOOTLOADER).elf $(BOOTLOADER).bin
	mkimage -f kernel.its $(BOOTLOADER).fit

	$(LD) -T link_loader_target.ld -o $(TARGET2).elf $(OBJS)
	$(OBJCOPY) -O binary $(TARGET2).elf $(TARGET2).bin

%.o: Src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.S
	$(CC) $(CFLAGS) -c $< -o $@

initramfs.cpio:
	cd rootfs && find . | cpio -o -H newc > ../initramfs.cpio

clean:
	rm -f *.o *.elf *.bin *.fit initramfs.cpio