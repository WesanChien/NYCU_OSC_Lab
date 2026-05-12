#include "uart.h"
#include "fdt.h"
#include "initrd.h"
#include "shell.h"
#include "mm.h"

void start_kernel(unsigned long fdt_addr) {
    const void *fdt = (const void *)fdt_addr;

    unsigned long uart_base = get_uart_base_from_dtb(fdt); // 先從 DTB 找 UART base address

    if (!uart_base) {
        while (1) { }
    }

    uart_init(uart_base);

    const void *rd_start = (const void *)dtb_get_u64_prop(
        fdt,
        "/chosen",
        "linux,initrd-start"
    );

    const void *rd_end = (const void *)dtb_get_u64_prop(
        fdt,
        "/chosen",
        "linux,initrd-end"
    );

    initrd_set_range(rd_start, rd_end);

    mm_init(fdt, (unsigned long)rd_start, (unsigned long)rd_end);

    uart_puts("\nStarting OSC loaded Lab3 advanced kernel ...\n");

    uart_puts("UART base from DTB: ");
    uart_hex(uart_base);
    uart_puts("\n");

    uart_puts("initrd-start: ");
    uart_hex((unsigned long)rd_start);
    uart_puts("\n");

    uart_puts("initrd-end:   ");
    uart_hex((unsigned long)rd_end);
    uart_puts("\n");

    uart_puts("Type 'help' for commands.\n");

    shell_run(fdt_addr);
}