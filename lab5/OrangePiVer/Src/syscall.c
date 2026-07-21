#include "syscall.h"
#include "trap.h"
#include "thread.h"
#include "uart.h"
#include "user.h"
#include "video.h"
#include "timer.h"
#include "signal.h"

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

    for (long i = 0; i < count; i++) {
        char c;

        /*
         * 沒有輸入時，不要卡在 kernel 裡 busy wait。
         * 主動 schedule()，讓 video child 或其他 process 可以繼續跑。
         */
        while (!uart_try_getc(&c)) {
            schedule();
        }

        buf[i] = c;
    }

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

static long sys_display(unsigned int *bmp_image, unsigned int width, unsigned int height) { // 讓 user program 可以請 kernel 把 frame 畫到 framebuffer
    return video_display(bmp_image, width, height);
}

/* 讓 video process 每張 frame 之間可以 sleep, 控制播放速度
 * 讓 user process 暫停一段時間, 但不要讓 CPU 空轉卡住
 */
static long sys_usleep(unsigned int usec) {
    unsigned long start = timer_get_time_us();

    while (timer_get_time_us() - start < usec) { // 時間還沒到就 schedule(), 讓其他 task 跑, 下次切回 video child，再檢查時間
        schedule();
    }

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

    case SYS_DISPLAY:
        return sys_display((unsigned int *)tf->a0, (unsigned int)tf->a1, (unsigned int)tf->a2);

    case SYS_USLEEP:
        return sys_usleep((unsigned int)tf->a0);

    case SYS_SIGNAL:
        return sys_signal((int)tf->a0, tf->a1);

    case SYS_SIGRETURN:
        return sys_sigreturn(tf);

    case SYS_KILL:
        return sys_kill(tf->a0, (int)tf->a1);
        
    default:
        uart_puts("unknown syscall: ");
        uart_dec(syscall_no);
        uart_puts("\n");
        return -1;
    }
}