#include "user.h"
#include "uart.h"
#include "thread.h"

static long user_syscall(long no, long a0, long a1, long a2) {
    register long _a0 asm("a0") = a0;
    register long _a1 asm("a1") = a1;
    register long _a2 asm("a2") = a2;
    register long _a7 asm("a7") = no;

    asm volatile("ecall"
                 : "+r"(_a0)
                 : "r"(_a1), "r"(_a2), "r"(_a7)
                 : "memory");

    return _a0;
}

static void user_puts(const char *s) {
    long len = 0;

    while (s[len])
        len++;

    /*
     * a7 = 2    SYS_UART_WRITE
     * a0 = s    buffer address
     * a1 = len  length
     * a2 = 0
     * ecall
     */
    user_syscall(2, (long)s, len, 0);
}

static long user_getpid(void) {
    return user_syscall(0, 0, 0, 0);
}

static void user_exit(int status) {
    user_syscall(6, status, 0, 0);

    while (1)
        ;
}

static void user_program(void) {
    user_puts("Hello from U-mode user program\n");

    long pid = user_getpid();

    user_puts("getpid syscall returned\n");

    /*
     * 先不印 pid 數字，避免還要做 user printf。
     */

    user_exit((int)pid);
}

void run_user_test(void) {
    uart_puts("create user process\n");

    struct task_struct *task = user_process_create(user_program);

    if (task == 0) {
        uart_puts("user_process_create failed\n");
        return;
    }

    while (task->state != TASK_ZOMBIE && task->state != TASK_UNUSED)
        schedule();

    kill_zombies();

    uart_puts("user process done\n");
}