RISCV_GNU ?= riscv64-unknown-elf
CC = $(RISCV_GNU)-gcc
LD = $(RISCV_GNU)-ld
OBJCOPY = $(RISCV_GNU)-objcopy
CFLAGS = -mcmodel=medany -ffreestanding -nostdlib -g -Wall -c

TARGET = kernel

build: clean
	$(CC) $(CFLAGS) -c *.S *.c
	$(LD) -T link_orangepi.ld -o $(TARGET).elf *.o
	$(OBJCOPY) -O binary $(TARGET).elf $(TARGET).bin
	mkimage -f kernel.its kernel.fit

run: build $(TARGET)
	$(QEMU) -M virt -m 8G -kernel $(TARGET) -display none -serial stdio

clean:
	rm -f $(TARGET) $(TARGET).elf $(TARGET).bin *.o kernel.fit
