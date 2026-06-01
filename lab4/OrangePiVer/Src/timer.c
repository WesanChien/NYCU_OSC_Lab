#include "timer.h"
#include "sbi.h"
#include "uart.h"

#define TIMER_INTERVAL_SEC   2UL
#define TIMER_FREQ 24000000UL

static unsigned long timer_freq = 0;
static volatile unsigned long boot_time_sec = 0;

/*
 * 讀 RISC-V time CSR。
 *
 * time CSR 是一個持續增加的 counter。
 * timer interrupt 要設定的是：
 * 下一次觸發時的 time value
 * 而不是 delay 本身。
 */
static inline unsigned long read_time(void) {
    unsigned long x;
    asm volatile("csrr %0, time" : "=r"(x));
    return x;
}

/*
 * 開啟 Supervisor Timer Interrupt Enable。
 *
 * sie.STIE = bit 5
 */
static inline void enable_stie(void) {
    asm volatile("csrs sie, %0" :: "r"(1UL << 5));
}

/*
 * 開啟 S-mode global interrupt。
 *
 * sstatus.SIE = bit 1
 */
static inline void enable_sie(void) {
    asm volatile("csrs sstatus, %0" :: "r"(1UL << 1));
}

/*
 * 設定下一次 timer interrupt。
 *
 * 重點：
 *     next = read_time() + timer_freq * 2
 *
 * 如果 timer_freq = 1,000,000，
 * 那 delta = 2,000,000 = 0x1e8480。
 */
static void timer_set_next(void) {
    unsigned long now = read_time();
    unsigned long delta = timer_freq * TIMER_INTERVAL_SEC;
    unsigned long next = now + delta;

    long err = sbi_set_timer(next);

    if (err != 0) {
        uart_puts("[timer] sbi_set_timer error: ");
        uart_hex(err);
        uart_puts("\n");
    }
}

void timer_init(void) {
    boot_time_sec = 0;

    /*
     * OrangePi RV2 實機 timebase。
     * 成功 demo repo 的 OrangePi fallback 也是 24 MHz。
     */
    timer_freq = TIMER_FREQ;

    timer_set_next();

    enable_stie();
    enable_sie();

    uart_puts("boot time: ");
    uart_dec(boot_time_sec);
    uart_puts("\n");
}

void timer_handle_interrupt(void) {
    boot_time_sec += TIMER_INTERVAL_SEC;

    uart_puts("boot time: ");
    uart_dec(boot_time_sec);
    uart_puts("\n");

    timer_set_next();
}
