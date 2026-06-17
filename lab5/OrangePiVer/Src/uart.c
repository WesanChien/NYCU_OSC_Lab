#include "uart.h"
#include "irq.h"

/*
 * OrangePi RV2 UART0.
 *
 * PXA-style UART:
 *   register shift = 2
 *   register width = 32-bit
 */
#define UART_DEFAULT_BASE 0xD4017000UL
#define REG_SHIFT         2 // OrangePi RV2 的每個 UART register 間隔 4 bytes

#define UART_BUF_SIZE     512

#define LSR_DR            (1 << 0) // Data Ready, UART receiver 有資料可以讀
#define LSR_TDRQ          (1 << 5) // Transmitter Data Request / THR(Transmitter Holding Register) Empty, UART transmitter 可以接受下一個字元了

#define IER_RX_ENABLE     (1 << 0)
#define IER_TX_ENABLE     (1 << 1)

#define MCR_OUT2          (1 << 3)

typedef volatile unsigned int uart_reg_t;

static unsigned long uart_base = UART_DEFAULT_BASE;

/*
 * Interrupt 版 UART 不適合直接讓 shell 去讀 hardware。
 * 因為字元可能在任何時間進來
 * 例如你正在跑 timer handler，使用者按了一個鍵。
 * 所以 ISR 要先把字元存起來。
 * 這個「先存起來」的地方就是 ring buffer：
 */
static char rx_buf[UART_BUF_SIZE];
static char tx_buf[UART_BUF_SIZE];

static volatile unsigned int rx_r = 0;
static volatile unsigned int rx_w = 0;
static volatile unsigned int tx_r = 0;
static volatile unsigned int tx_w = 0;

static volatile int uart_async_enabled = 0;
static volatile int rx_overflow = 0;

/*
 * Read the Receiver Buffer Register (RBR).
 * 讀收到的字元
 */
static inline uart_reg_t *uart_rbr(void) {
    return (uart_reg_t *)(uart_base + (0x0 << REG_SHIFT));
}

/*
 * Read the Transmitter Holding Register (THR).
 * 寫要送出的字元
 */
static inline uart_reg_t *uart_thr(void) {
    return (uart_reg_t *)(uart_base + (0x0 << REG_SHIFT));
}

/*
 * Read the Interrupt Enable Register (IER).
 * 用來 enable/disable UART interrupt
 */
static inline uart_reg_t *uart_ier(void) {
    return (uart_reg_t *)(uart_base + (0x1 << REG_SHIFT));
}

/*
 * Read the Line Status Register (LSR).
 * 檢查 UART 狀態。
 */
static inline uart_reg_t *uart_lsr(void) {
    return (uart_reg_t *)(uart_base + (0x5 << REG_SHIFT));
}

/*
 * Read the Modem Control Register (MCR).
 * 用來控制 UART modem 功能。
 */
static inline uart_reg_t *uart_mcr(void) {
    return (uart_reg_t *)(uart_base + (0x4 << REG_SHIFT));
}

static inline unsigned int next_idx(unsigned int idx) { // 只是要循環使用 ring buffer 的 index
    return (idx + 1U) % UART_BUF_SIZE;
}

static void uart_enable_tx_interrupt(void) {
    *uart_ier() |= IER_TX_ENABLE;
}

static void uart_disable_tx_interrupt(void) {
    *uart_ier() &= ~IER_TX_ENABLE;
}

/*
 * interrupts disabled 狀態下呼叫。
 * 把 tx ring buffer 裡的資料送到 UART THR。
 */
static void uart_tx_kick_locked(void) {
    while (tx_r != tx_w && ((*uart_lsr() & LSR_TDRQ) != 0)) { // tx_buffer 中有資料 && UART 可以接收下一個要送出的字元
        *uart_thr() = (uart_reg_t)tx_buf[tx_r];
        tx_r = next_idx(tx_r);
    }

    if (tx_r == tx_w)
        uart_disable_tx_interrupt(); // 沒資料要送時，開 TX interrupt 可能會一直觸發，浪費 CPU 時間
    else
        uart_enable_tx_interrupt();
}

void uart_init(unsigned long base) {
    uart_base = base ? base : UART_DEFAULT_BASE; // 設定 UART base address

    rx_r = rx_w = 0;
    tx_r = tx_w = 0;
    rx_overflow = 0;
    uart_async_enabled = 0; // 預設先用 polling 模式，等 shell 跑起來再切換到 interrupt 模式

    *uart_mcr() |= MCR_OUT2; // 設定 MCR_OUT2，讓 UART interrupt 能送出去
}

void uart_enable_interrupt(void) {
    unsigned long s = local_irq_save();

    rx_r = rx_w = 0;
    tx_r = tx_w = 0;
    rx_overflow = 0;

    uart_async_enabled = 1; // 表示之後 uart_getc() / uart_putc() 走 buffer 模式

    /*
     * RX interrupt enable。
     * TX interrupt 先不開，等 tx buffer 有資料時再開
     */
    *uart_ier() |= IER_RX_ENABLE;
    *uart_ier() &= ~IER_TX_ENABLE;

    local_irq_restore(s);
}

/*
 * UART ISR
 *
 * 這個 function 會在 PLIC claim 到 UART0_IRQ_ID 後被呼叫。
 */
void uart_handle_irq(void) {
    unsigned long s = local_irq_save();

    /*
     * RX：只要 hardware 有資料，就全部搬到 rx ring buffer。
     */
    while ((*uart_lsr() & LSR_DR) != 0) { // RX: UART 有收到資料
        char c = (char)(*uart_rbr());
        unsigned int n = next_idx(rx_w);

        if (n != rx_r) {
            rx_buf[rx_w] = c;
            rx_w = n;
        } else { // rx_w + 1 == rx_r，代表 rx buffer 已經滿了
            rx_overflow++;
        }
    }

    uart_tx_kick_locked(); // TX：如果 UART transmitter ready，就把 tx ring buffer 的資料送出去

    local_irq_restore(s);
}

char uart_getc(void) {
    if (!uart_async_enabled) {
        while ((*uart_lsr() & LSR_DR) == 0)
            ;
        char c = (char)(*uart_rbr());
        return c == '\r' ? '\n' : c;
    }

    while (1) {
        unsigned long s = local_irq_save(); // 關掉 interrupt，確保接下來讀 rx ring buffer 的時候不會被 ISR 打斷

        if (rx_r != rx_w) {
            char c = rx_buf[rx_r];
            rx_r = next_idx(rx_r);
            local_irq_restore(s); // 讀 rx ring buffer 完了再開 interrupt，確保不會漏掉剛剛讀的這個字元之後的 interrupt
            return c == '\r' ? '\n' : c;
        }

        local_irq_restore(s); // rx ring buffer 裡沒有資料了，先開 interrupt 等 ISR 把資料放進來

        /*
         * 保險：如果 interrupt 還沒進來但 LSR 已經有資料，
         * 直接手動 drain 一次。
         */
        if ((*uart_lsr() & LSR_DR) != 0)
            uart_handle_irq();
    }
}

char uart_getc_raw(void) {
    if (!uart_async_enabled) {
        while ((*uart_lsr() & LSR_DR) == 0)
            ;
        return (char)(*uart_rbr());
    }

    while (1) {
        unsigned long s = local_irq_save();

        if (rx_r != rx_w) {
            char c = rx_buf[rx_r];
            rx_r = next_idx(rx_r);
            local_irq_restore(s);
            return c;
        }

        local_irq_restore(s);

        if ((*uart_lsr() & LSR_DR) != 0)
            uart_handle_irq();
    }
}

void uart_putc(char c) {
    if (c == '\n')
        uart_putc('\r');

    /*
     * TX output 使用 polling。
     * 不依賴 TX interrupt，避免在 trap/syscall context 內輸出卡住。
     */
    while ((*uart_lsr() & LSR_TDRQ) == 0)
        ;

    *uart_thr() = (uart_reg_t)c;
}

// void uart_putc(char c) {
//     if (c == '\n')
//         uart_putc('\r');

//     if (!uart_async_enabled) {
//         while ((*uart_lsr() & LSR_TDRQ) == 0)
//             ;
//         *uart_thr() = (uart_reg_t)c;
//         return;
//     }

//     while (1) {
//         unsigned long s = local_irq_save();
//         unsigned int n = next_idx(tx_w);

//         if (n != tx_r) {
//             tx_buf[tx_w] = c;
//             tx_w = n;

//             uart_enable_tx_interrupt();
//             uart_tx_kick_locked();

//             local_irq_restore(s);
//             return;
//         }

//         /*
//          * tx buffer 滿了。
//          * 先嘗試 kick TX，釋放一些空間。
//          */
//         uart_tx_kick_locked();
//         local_irq_restore(s);
//     }
// }

void uart_puts(const char *s) {
    while (*s)
        uart_putc(*s++);
}

void uart_hex(unsigned long x) {
    uart_puts("0x");
    for (int i = 60; i >= 0; i -= 4) {
        unsigned long n = (x >> i) & 0xf;
        uart_putc(n < 10 ? '0' + n : 'a' + n - 10);
    }
}

void uart_dec(unsigned long x) {
    char buf[32];
    int i = 0;

    if (x == 0) {
        uart_putc('0');
        return;
    }

    while (x > 0) {
        buf[i++] = '0' + (x % 10);
        x /= 10;
    }

    while (i > 0)
        uart_putc(buf[--i]);
}