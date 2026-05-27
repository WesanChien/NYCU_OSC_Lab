#include "trap.h"
#include "uart.h"
#include "timer.h"

#define SCAUSE_INTERRUPT_MASK       (1UL << 63)
#define SCAUSE_ECALL_FROM_U         8UL
#define SCAUSE_SUPERVISOR_TIMER     5UL

extern void handle_exception(void);

void trap_init(void) {
    asm volatile("csrw stvec, %0" :: "r"(handle_exception));
}

void do_trap(struct trap_frame *tf) {
    unsigned long is_interrupt = tf->scause & SCAUSE_INTERRUPT_MASK;
    unsigned long cause = tf->scause & ~SCAUSE_INTERRUPT_MASK;

    if (is_interrupt && cause == SCAUSE_SUPERVISOR_TIMER) {
        timer_handle_interrupt();
        return;
    }

    if (!is_interrupt && cause == SCAUSE_ECALL_FROM_U) {
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
        return;
    }

    uart_puts("[trap] unhandled\n");
    uart_puts("scause: ");
    uart_hex(tf->scause);
    uart_puts("\n");

    while (1)
        asm volatile("wfi");
}