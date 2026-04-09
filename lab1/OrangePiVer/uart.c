#define UART_BASE 0xD4017000UL // OrangePi 實際 console 對應的 UART0 base address
#define REG_SHIFT 2 // OrangePi 的 UART register 是 32-bit 寬的，但實際上只使用了最低 8 位，因此每個 register 之間的地址間隔是 word-spaced 4 bytes (2^2)，所以 REG_SHIFT 是 2
#define LSR_DR    (1U << 0)
#define LSR_TDRQ  (1U << 6)
typedef volatile unsigned int uart_reg_t; // MMIO register 不是普通變數，編譯器不能把它優化掉，所以用 volatile 

#define UART_RBR ((uart_reg_t *)(UART_BASE + (0x0 << REG_SHIFT))) // 以 32-bit word 去讀寫 register
#define UART_THR ((uart_reg_t *)(UART_BASE + (0x0 << REG_SHIFT)))
#define UART_LSR ((uart_reg_t *)(UART_BASE + (0x5 << REG_SHIFT))) // 0x14

char uart_getc(void) {
    while (((*UART_LSR) & LSR_DR) == 0)
        ;
    char c = (char)(*UART_RBR & 0xff); // &0xff 是只取最低 8 位
    return (c == '\r') ? '\n' : c;
}

void uart_putc(char c) {
    if (c == '\n') {
        while (((*UART_LSR) & LSR_TDRQ) == 0)
            ;
        *UART_THR = '\r';
    }

    while (((*UART_LSR) & LSR_TDRQ) == 0)
        ;
    *UART_THR = (unsigned int)(unsigned char)c;
}

void uart_puts(const char *s) {
    while (*s)
        uart_putc(*s++);
}

void uart_hex(unsigned long h) {
    uart_puts("0x");
    for (int c = 60; c >= 0; c -= 4) {
        unsigned long n = (h >> c) & 0xf;
        n += (n > 9) ? 0x57 : '0';
        uart_putc((char)n);
    }
}