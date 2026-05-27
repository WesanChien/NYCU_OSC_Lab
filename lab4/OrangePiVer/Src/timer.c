#include "timer.h"
#include "sbi.h"
#include "uart.h"

/*
 * OrangePi RV2 / K1 Generic Counter register.
 *
 * CNTFID_REG 儲存 counter frequency。
 * 你前面實測讀到：
 *     0x00000000000f4240 = 1,000,000 Hz
 *
 * 代表 time CSR 每秒增加 1,000,000。
//  */
// #define K1_COUNTER_BASE      0xD5001000UL
// #define K1_CNTFID_REG        (K1_COUNTER_BASE + 0x20)

/*
 * Basic Exercise 2 要求每 2 秒觸發一次 timer interrupt。
 */
#define TIMER_INTERVAL_SEC   2UL
#define TIMER_FREQ 24000000UL
/*
 * 設成 1 可以印 now / delta / next debug。
 * 跑通後建議設 0，避免畫面太亂。
 */
// #define TIMER_DEBUG          0

static unsigned long timer_freq = 0;
static volatile unsigned long boot_time_sec = 0;

/*
 * 讀 RISC-V time CSR。
 *
 * time CSR 是一個持續增加的 counter。
 * timer interrupt 要設定的是：
 *     下一次觸發時的 time value
 * 而不是 delay 本身。
 */
static inline unsigned long read_time(void) {
    unsigned long x;
    asm volatile("csrr %0, time" : "=r"(x));
    return x;
}

// static inline unsigned long read_sie(void) {
//     unsigned long x;
//     asm volatile("csrr %0, sie" : "=r"(x));
//     return x;
// }

// static inline unsigned long read_sstatus(void) {
//     unsigned long x;
//     asm volatile("csrr %0, sstatus" : "=r"(x));
//     return x;
// }

/*
 * 從 K1 counter frequency register 讀 timer frequency。
 *
 * 如果讀到 0，先 fallback 成你實測的 1 MHz。
 */
// static unsigned long read_counter_freq(void) {
//     unsigned int freq = *(volatile unsigned int *)K1_CNTFID_REG;

//     if (freq == 0)
//         freq = 1000000UL;

//     return (unsigned long)freq;
// }

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

// unsigned long timer_get_boot_time(void) {
//     return boot_time_sec;
// }