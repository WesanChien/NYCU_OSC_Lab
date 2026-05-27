#include "loader.h"
#include "uart.h"

#define LOAD_ADDR  0x20000000UL
#define BOOT_MAGIC 0x544F4F42UL

static unsigned int uart_get_u32_le(void) {
    unsigned int b0 = (unsigned char)uart_getc();
    unsigned int b1 = (unsigned char)uart_getc();
    unsigned int b2 = (unsigned char)uart_getc();
    unsigned int b3 = (unsigned char)uart_getc();

    return b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
}

static void uart_put_u32_hex(unsigned int x) {
    uart_hex((unsigned long)x);
}

void load_image_and_boot(unsigned long fdt_addr) {
    unsigned int magic;
    unsigned int size;
    unsigned char *dst = (unsigned char *)LOAD_ADDR;

    uart_puts("Waiting for image header...\n");

    magic = uart_get_u32_le();
    size  = uart_get_u32_le();

    if (magic != BOOT_MAGIC) {
        uart_puts("Bad magic: ");
        uart_put_u32_hex(magic);
        uart_puts("\n");
        return;
    }

    uart_puts("Image size: ");
    uart_put_u32_hex(size);
    uart_puts("\n");

    for (unsigned int i = 0; i < size; i++)
        dst[i] = (unsigned char)uart_getc();

    uart_puts("Image loaded at ");
    uart_hex(LOAD_ADDR);
    uart_puts("\nJumping...\n");

    asm volatile(
        "mv a1, %0\n"
        "jr %1\n"
        :
        : "r"(fdt_addr), "r"(LOAD_ADDR)
        : "a1", "memory"
    );
}