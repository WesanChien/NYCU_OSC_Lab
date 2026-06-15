#include "trap.h"
#include "uart.h"
#include "timer.h"
#include "plic.h"
#include "uart.h"
#include "task.h"

#define SCAUSE_INTERRUPT_MASK       (1UL << 63)
#define SCAUSE_ECALL_FROM_U         8UL
#define SCAUSE_SUPERVISOR_TIMER     5UL

#define SCAUSE_INTERRUPT_MASK        (1UL << 63)
#define SCAUSE_SUPERVISOR_TIMER      5UL
#define SCAUSE_SUPERVISOR_EXTERNAL   9UL
#define SCAUSE_ECALL_FROM_U          8UL

#define UART0_IRQ_ID                 42U

extern void handle_exception(void);

void trap_init(void) {
    asm volatile("csrw stvec, %0" :: "r"(handle_exception));
}

void do_trap(struct trap_frame *tf) {
    unsigned long is_interrupt = tf->scause & SCAUSE_INTERRUPT_MASK;
    unsigned long cause = tf->scause & ~SCAUSE_INTERRUPT_MASK;
    int handled = 0;

    if (is_interrupt && cause == SCAUSE_SUPERVISOR_TIMER) {
        timer_handle_interrupt();
        handled = 1;
    } 
    else if (is_interrupt && cause == SCAUSE_SUPERVISOR_EXTERNAL) {
        unsigned int irq = plic_claim();

        if (irq == UART0_IRQ_ID) {
            uart_handle_irq();
        }

        if (irq != 0) {
            plic_complete(irq);
        }

        handled = 1;
    } 
    else if (!is_interrupt && cause == SCAUSE_ECALL_FROM_U) {
        uart_puts("=== S-mode trap ===\n");

        uart_puts("scause: ");
        uart_dec(tf->scause);
        uart_puts("\n");

        uart_puts("sepc: ");
        uart_hex(tf->sepc);
        uart_puts("\n");

        uart_puts("stval: ");
        uart_hex(tf->stval);
        uart_puts("\n");

        tf->sepc += 4;
        handled = 1;
    }

    if (!handled) {
        uart_puts("[trap] unhandled trap\n");
        uart_puts("scause: ");
        uart_hex(tf->scause);
        uart_puts("\n");

        uart_puts("sepc: ");
        uart_hex(tf->sepc);
        uart_puts("\n");

        uart_puts("stval: ");
        uart_hex(tf->stval);
        uart_puts("\n");

        while (1)
            asm volatile("wfi");
    }

    /*
     * interrupt handler 結束後，不要立刻 sret。
     * 先執行 pending tasks。
     */
    run_pending_task();
}