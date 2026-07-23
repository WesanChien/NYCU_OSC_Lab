#include "trap.h"
#include "uart.h"
#include "timer.h"
#include "plic.h"
#include "uart.h"
#include "task.h"
#include "syscall.h"
#include "thread.h"
#include "signal.h"

#define SCAUSE_INTERRUPT_MASK        (1UL << 63)
#define SCAUSE_SUPERVISOR_TIMER      5UL
#define SCAUSE_SUPERVISOR_EXTERNAL   9UL
#define SCAUSE_ECALL_FROM_U          8UL
#define SSTATUS_SPP                  (1UL << 8) // Supervisor Previous Privilege

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

        /*
         * 只在 timer interrupt 打斷 user process/U-mode 時才做 preemption。
         * 如果 timer interrupt 發生在 kernel/S-mode 裡，
         * 先不要切，避免在 kernel critical section 中途被切走。
         */
        if ((tf->sstatus & SSTATUS_SPP) == 0) { // bit 8 == 0, 代表 U-mode
            schedule();
        }
        
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
        /*
         * user ecall 的 sepc 指向 ecall 指令本身。
         * 必須 +4，否則 sret 回 user 後會重複執行同一個 ecall。
         */
        tf->sepc += 4;

        long syscall_no = tf->a7; 
        /*
         * syscall number 在 a7。
         * syscall 參數在 a0, a1, a2...
         * return value 要寫回 a0。
         */
        long ret = syscall_handler(tf);
        /*
         * sigreturn 會直接恢復整份 trapframe。
         * 不可以再覆寫 a0。
         */
        if (syscall_no != SYS_SIGRETURN) {
            tf->a0 = ret;
        }
        
        handled = 1;
    }

    /*
     * 回 U-mode 前檢查 pending signal。
     * signal 在這些情況被送出：
     * 1. user process syscall 結束，準備回 user mode 前。
     * 2. user process 被 timer interrupt 打斷，準備回 user mode 前。
     * 3. user process 被 UART/external interrupt 打斷，準備回 user mode 前。
     */
    if (handled && (tf->sstatus & SSTATUS_SPP) == 0) { // SPP=0 代表這次 trap 原本來自 U-mode
        signal_try_deliver(tf);
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