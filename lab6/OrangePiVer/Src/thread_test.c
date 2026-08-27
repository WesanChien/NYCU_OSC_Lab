#include "thread.h"
#include "uart.h"

static void uart_put_uint(unsigned long x) {
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

static void test_thread(void) {
    for (int i = 0; i < 5; i++) {
        uart_puts("Thread id: ");
        uart_put_uint(get_current()->pid);
        uart_puts(" ");
        uart_put_uint(i);
        uart_puts("\n");

        for (volatile int j = 0; j < 100000000; j++)
            ;

        schedule();
    }

    thread_exit();
}

static int thread_done(struct task_struct *task) {
    return task == 0 ||
           task->state == TASK_ZOMBIE ||
           task->state == TASK_UNUSED;
}

static int all_threads_done(struct task_struct **threads, int count) {
    for (int i = 0; i < count; i++) {
        if (!thread_done(threads[i]))
            return 0;
    }

    return 1;
}

void run_thread_test(void) {
    uart_puts("create 3 test threads\n");

    struct task_struct *threads[3];
    int count = 0;

    for (int i = 0; i < 3; i++) {
        struct task_struct *task = thread_create(test_thread);

        if (task == 0) {
            uart_puts("thread_create failed\n");
            continue;
        }

        threads[count++] = task;
    }

    /*
     * 不能只 schedule 一次。
     * 要反覆讓出 CPU，直到這次建立的 thread 都變成 ZOMBIE / UNUSED。
     */
    while (!all_threads_done(threads, count)) {
        schedule();
    }

    kill_zombies();

    uart_puts("thread test done\n");
}