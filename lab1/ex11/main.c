#define UART_BASE 0x10000000UL // QEMU virt machine 的 UART base address 是 0x10000000
#define UART_RBR (unsigned char*)(UART_BASE + 0x0) // Receiver Buffer Register for reading received data
#define UART_THR (unsigned char*)(UART_BASE + 0x0) // Transmitter Holding Register for sending data
#define UART_LSR (unsigned char*)(UART_BASE + 0x5) // Line Status Register
/* 
你對 *UART_THR = 'A';，本質上是在對 UART 硬體說：請送出字元 A
*/

#define LSR_DR   (1 << 0) // LSR_DR 表示 data ready
#define LSR_TDRQ (1 << 5)

char uart_getc() {
    while ((*UART_LSR & LSR_DR) == 0) // Busy wait until data is available in the RBR
        ; 
    char c = (char)*UART_RBR;
    return (c == '\r') ? '\n' : c;
}

void uart_putc(char c) {
    if (c == '\n')
        uart_putc('\r'); // Convert Line Feed to Carriage Return for proper terminal display (\n -> \r\n)

    while ((*UART_LSR & LSR_TDRQ) == 0) // 檢查 LSR_TDRQ，確定 transmitter 可以送
        ;
    *UART_THR = c;
}

void uart_puts(const char* s) { // 逐字送出，直到遇到字串結尾 '\0'
    while (*s)
        uart_putc(*s++);
}

void start_kernel() {
    uart_puts("\nStarting OSC kernel ...\n");

    while (1) {
        uart_putc(uart_getc());
    }
}