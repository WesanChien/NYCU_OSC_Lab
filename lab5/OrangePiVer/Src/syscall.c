#include "syscall.h"
#include "thread.h"
#include "uart.h"

static long sys_getpid(void) {
    return get_current()->pid;
}

static long sys_uart_write(const char *buf, long count) {
    if (buf == 0 || count < 0)
        return -1;

    for (long i = 0; i < count; i++)
        uart_putc(buf[i]);

    return count;
}

static long sys_uart_read(char *buf, long count) {
    if (buf == 0 || count < 0)
        return -1;

    for (long i = 0; i < count; i++)
        buf[i] = uart_getc();

    return count;
}

static long sys_exit(int status) {
    get_current()->exit_status = status;
    thread_exit();

    return 0;
}

long syscall_handler(struct trap_frame *tf) {
    long syscall_no = tf->a7;

    switch (syscall_no) {
    case SYS_GETPID:
        return sys_getpid();

    case SYS_UART_READ:
        return sys_uart_read((char *)tf->a0, tf->a1);

    case SYS_UART_WRITE:
        return sys_uart_write((const char *)tf->a0, tf->a1);

    case SYS_EXIT:
        return sys_exit((int)tf->a0);

    case SYS_EXEC:
    case SYS_FORK:
    case SYS_WAITPID:
    case SYS_STOP:
        uart_puts("syscall not implemented yet\n");
        return -1;

    default:
        uart_puts("unknown syscall\n");
        return -1;
    }
}