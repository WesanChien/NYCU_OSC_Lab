// #include "uart.h"
// #include "fdt.h"
// #include "initrd.h"
// #include "shell.h"
// #include "mm.h"
// #include "trap.h"
// #include "timer.h"
// #include "sbi.h"
// #include "plic.h"
// #include "task.h"
// #include "thread.h"

// void start_kernel(unsigned long fdt_addr) {
//     const void *fdt = (const void *)fdt_addr;

//     unsigned long uart_base = get_uart_base_from_dtb(fdt); // 先從 DTB 找 UART base address

//     if (!uart_base) {
//         while (1) { }
//     }

//     uart_init(uart_base);

//     const void *rd_start = (const void *)dtb_get_u64_prop(
//         fdt,
//         "/chosen",
//         "linux,initrd-start"
//     );

//     const void *rd_end = (const void *)dtb_get_u64_prop(
//         fdt,
//         "/chosen",
//         "linux,initrd-end"
//     );

//     initrd_set_range(rd_start, rd_end);

//     mm_init(fdt, (unsigned long)rd_start, (unsigned long)rd_end);

//     uart_puts("\nStarting OSC Lab5 ex2 kernel ...\n");

//     uart_puts("Type 'help' for commands.\n");

//     trap_init(); // 設 stvec, 不然 interrupt 來了不知道跳哪裡

//     task_init(); // 初始化 task queue

//     timer_init(); // 開 timer interrupt
    
//     plic_init(0); // 設 PLIC (UART0 IRQ priority / enable / threshold)
//     uart_enable_interrupt(); // 開 UART RX interrupt
//     enable_external_interrupt(); // 開 sie.SEIE / sstatus.SIE, 允許 external interrupt 進 CPU

//     thread_init();

//     trap_init();

//     shell_run(fdt_addr);
// }
#include "uart.h"
#include "fdt.h"
#include "initrd.h"
#include "mm.h"
#include "shell.h"
#include "vm.h"

void start_kernel(unsigned long fdt_addr)
{
    const void *fdt = (const void *)fdt_addr;

    /*
     * DTB 裡讀到的是 UART physical address。
     */
    unsigned long uart_pa =
        get_uart_base_from_dtb(fdt);

    if (!uart_pa) {
        while (1) {
        }
    }

    /*
     * CPU 現在不能直接使用 PA，因此轉成
     * kernel higher-half VA。
     */
    unsigned long uart_va =
        phys_to_virt_addr(uart_pa);

    uart_init(uart_va);

    uart_puts("\n[VM] Higher-half kernel is running\n");

    /*
     * DTB property 本身保存 physical address。
     */
    unsigned long rd_start_pa =
        dtb_get_u64_prop(
            fdt,
            "/chosen",
            "linux,initrd-start"
        );

    unsigned long rd_end_pa =
        dtb_get_u64_prop(
            fdt,
            "/chosen",
            "linux,initrd-end"
        );

    /*
     * initrd parser 會直接讀 archive memory，
     * 所以它需要的是可以 dereference 的 VA。
     */
    const void *rd_start_va =
        (const void *)phys_to_virt_addr(rd_start_pa);

    const void *rd_end_va =
        (const void *)phys_to_virt_addr(rd_end_pa);

    initrd_set_range(
        rd_start_va,
        rd_end_va
    );

    /*
     * mm_init 裡管理的仍是 physical memory。
     */
    mm_init(
        fdt,
        rd_start_pa,
        rd_end_pa
    );

    uart_puts("[VM] Memory initialized\n");

    /*
     * 暫時不開 timer / PLIC / thread。
     */
    shell_run(fdt_addr);
    

    while (1) {
    }
}