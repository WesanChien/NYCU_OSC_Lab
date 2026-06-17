#include "uart.h"
#include "fdt.h"
#include "initrd.h"
#include "shell.h"
#include "mm.h"
#include "trap.h"
#include "timer.h"
#include "sbi.h"
#include "plic.h"
#include "task.h"
#include "thread.h"

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

    uart_puts("\nStarting OSC Lab5 ex1 kernel ...\n");

    uart_puts("Type 'help' for commands.\n");

    trap_init(); // 設 stvec, 不然 interrupt 來了不知道跳哪裡

    task_init(); // 初始化 task queue

    timer_init(); // 開 timer interrupt
    
    plic_init(0); // 設 PLIC (UART0 IRQ priority / enable / threshold)
    uart_enable_interrupt(); // 開 UART RX interrupt
    enable_external_interrupt(); // 開 sie.SEIE / sstatus.SIE, 允許 external interrupt 進 CPU

    thread_init();

    trap_init();

    shell_run(fdt_addr);
}