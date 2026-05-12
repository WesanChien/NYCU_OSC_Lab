#ifndef UART_H
#define UART_H

char uart_getc(void);
void uart_putc(char c);
void uart_puts(const char* s);
void uart_hex(unsigned long h);
void uart_init(unsigned long base);

#endif