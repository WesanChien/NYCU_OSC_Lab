extern char uart_getc(void);
extern void uart_putc(char c);
extern void uart_puts(const char* s);
extern void uart_hex(unsigned long h);

#define UART_BASE 0x10000000UL
#define UART_RBR  (unsigned char*)(UART_BASE + 0x0)
#define UART_THR  (unsigned char*)(UART_BASE + 0x0)
#define UART_IER  (unsigned char*)(UART_BASE + 0x1)
#define UART_IIR  (unsigned char*)(UART_BASE + 0x2)
#define UART_MCR  (unsigned char*)(UART_BASE + 0x4)
#define UART_LSR  (unsigned char*)(UART_BASE + 0x5)
#define LSR_DR    (1 << 0)
#define LSR_TDRQ  (1 << 5)
#define UART_IRQ  0x0a

#define PLIC_BASE            0xc000000UL
#define PLIC_PRIORITY(irq)   (PLIC_BASE + (irq) * 4)
#define PLIC_ENABLE(hart)    (PLIC_BASE + 0x002080 + (hart) * 0x0100)
#define PLIC_THRESHOLD(hart) (PLIC_BASE + 0x201000 + (hart) * 0x2000)
#define PLIC_CLAIM(hart)     (PLIC_BASE + 0x201004 + (hart) * 0x2000)

#define REG32(addr) (*(volatile unsigned int *)(addr))
#define REG8(addr)  (*(volatile unsigned char *)(addr))

unsigned long boot_cpu_hartid = 0;

void uart_init() {
    *UART_IER |= 1 << 0; // UART hardware will trigger an interrupt when buffer receives data.
    *UART_MCR |= 1 << 3; // Allows the UART interrupt signal to be routed to PLIC.
}

void irq_enable() {
    asm volatile("csrsi sstatus, (1 << 1)");
}

void enable_external_interrupt() {
    asm volatile(
        "li t0, (1 << 9);"
        "csrs sie, t0;");
}

void plic_init() {
    REG32(PLIC_PRIORITY(UART_IRQ)) = 1; // Set UART interrupt priority to 1 (non-zero to enable)

    REG32(PLIC_ENABLE(boot_cpu_hartid)) |= (1 << UART_IRQ); // Enable UART interrupt for the boot hart

    REG32(PLIC_THRESHOLD(boot_cpu_hartid)) = 0; // Set threshold to 0 to allow all interrupts with priority > 0

    enable_external_interrupt(); // Enable external interrupts in the CPU's interrupt controller
}

int plic_claim() {
    return REG32(PLIC_CLAIM(boot_cpu_hartid));
}

void plic_complete(int irq) {
    REG32(PLIC_CLAIM(boot_cpu_hartid)) = irq;
}

void do_trap() {
    int irq = plic_claim();
    if (irq == UART_IRQ) {
        char c = *UART_RBR;
        uart_putc(c == '\r' ? '\n' : c);
    }
    if (irq)
        plic_complete(irq);
}

void start_kernel() {
    uart_puts("\nStarting Lab4 kernel ...\n");
    plic_init();
    uart_init();
    irq_enable();
    while (1)
        ;
}
