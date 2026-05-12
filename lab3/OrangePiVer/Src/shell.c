#include "shell.h"
#include "uart.h"
#include "kstring.h"
#include "sbi.h"
#include "initrd.h"
#include "loader.h"
#include "mm.h"

static void print_help(void) {
    uart_puts("Available commands:\n");
    uart_puts("  help        - show this help message\n");
    uart_puts("  hello       - print Hello World!\n");
    uart_puts("  info        - show OpenSBI information\n");
    uart_puts("  ls          - list files in initramfs\n");
    uart_puts("  cat <file>  - show file content from initramfs\n");
    uart_puts("  load        - load a kernel image over UART and boot it\n");
    uart_puts("  memdump     - show memory allocator state\n");
    uart_puts("  memtest     - run memory allocator test\n");
}

void shell_run(unsigned long fdt_addr) {
    char buf[64];
    int idx;

    while (1) {
        uart_puts("OrangePi RV2> ");
        idx = 0;

        while (1) {
            char c = uart_getc();

            if (c == '\n') {
                uart_putc('\n');
                buf[idx] = '\0';
                break;
            }

            if (c == '\b' || c == 127) {
                if (idx > 0) {
                    idx--;
                    uart_puts("\b \b");
                }
                continue;
            }

            if (idx < (int)sizeof(buf) - 1) {
                buf[idx++] = c;
                uart_putc(c);
            }
        }

        if (str_eq(buf, "help")) {
            print_help();
        } else if (str_eq(buf, "hello")) {
            uart_puts("Hello World!\n");
        } else if (str_eq(buf, "info")) {
            print_info();
        } else if (str_eq(buf, "ls")) {
            initrd_list();
        } else if (starts_with(buf, "cat")) {
            const char *name = skip_spaces(buf + 3);

            if (*name == '\0')
                uart_puts("Usage: cat <file>\n");
            else
                initrd_cat(name);
        } else if (str_eq(buf, "load")) {
            load_image_and_boot(fdt_addr);
        } else if (str_eq(buf, "memdump")) {
            mm_dump();
        } else if (str_eq(buf, "memtest")) {
            mm_test();
        } else if (buf[0] != '\0') {
            uart_puts("Unknown command: ");
            uart_puts(buf);
            uart_puts("\n");
        }
    }
}