static unsigned long uart_base = 0;
#define REG_SHIFT 2
#define LSR_DR    (1U << 0)
#define LSR_TDRQ  (1U << 6)

typedef volatile unsigned int uart_reg_t;

static inline uart_reg_t *uart_reg(unsigned long off) {
    return (uart_reg_t *)(uart_base + (off << REG_SHIFT));
}

void uart_init(unsigned long base) {
    uart_base = base;
}

char uart_getc(void) {
    while (((*uart_reg(0x5)) & LSR_DR) == 0)
        ;
    char c = (char)(*uart_reg(0x0) & 0xff);
    return (c == '\r') ? '\n' : c;
}

void uart_putc(char c) {
    if (c == '\n') {
        while (((*uart_reg(0x5)) & LSR_TDRQ) == 0)
            ;
        *uart_reg(0x0) = '\r';
    }

    while (((*uart_reg(0x5)) & LSR_TDRQ) == 0)
        ;
    *uart_reg(0x0) = (unsigned int)(unsigned char)c;
}

void uart_puts(const char *s) {
    while (*s)
        uart_putc(*s++);
}

void uart_hex(unsigned long h) {
    uart_puts("0x");
    for (int c = (int)(sizeof(unsigned long) * 8) - 4; c >= 0; c -= 4) {
        unsigned long n = (h >> c) & 0xf;
        n += (n > 9) ? 0x57 : '0';
        uart_putc((char)n);
    }
}