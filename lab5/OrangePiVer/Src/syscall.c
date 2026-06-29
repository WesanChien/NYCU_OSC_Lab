#include "syscall.h"
#include "trap.h"
#include "thread.h"
#include "uart.h"
#include "user.h"

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

static long sys_exec(const char *path, struct trap_frame *tf) {
    return user_exec_current(path, tf);
}

static long sys_fork(struct trap_frame *tf) {
    return user_fork(tf);
}

static long sys_waitpid(long pid) {
    struct task_struct *current = get_current();
    struct task_struct *child = task_find_by_pid(pid);

    if (child == 0) {
        return -1;
    }

    uart_puts("\n");

    if (child->parent != current) {
        return -1;
    }

    /*
     * 如果 child 還沒結束，parent 就 schedule() 讓出 CPU。
     * child 才能跑、才能 exit()。
     */
    while (child->state != TASK_ZOMBIE && child->state != TASK_UNUSED) {
        schedule();
    }

    if (child->state == TASK_ZOMBIE)
        task_reap(child);

    return pid;
}

static long sys_exit(int status) {
    struct task_struct *current = get_current();

    current->exit_status = status;
    thread_exit();

    uart_puts("[sys_exit] should not return\n");

    return 0;
}

/*
 * stop(別人) 就是變成 zombie，stop() 自己等同於 exit()
 */
static long sys_stop(long pid) {
    return task_kill(pid, -1);
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

    case SYS_EXEC:
        return sys_exec((const char *)tf->a0, tf);

    case SYS_FORK:
        return sys_fork(tf);

    case SYS_WAITPID:
        return sys_waitpid(tf->a0);

    case SYS_EXIT:
        return sys_exit((int)tf->a0);

    case SYS_STOP:
        return sys_stop(tf->a0);

    default:
        uart_puts("unknown syscall: ");
        uart_dec(syscall_no);
        uart_puts("\n");
        return -1;
    }
}