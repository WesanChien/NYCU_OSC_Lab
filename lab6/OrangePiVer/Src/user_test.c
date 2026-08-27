#include "user.h"
#include "user_test.h"
#include "uart.h"
#include "thread.h"

/*
 * User syscall wrapper, 透過 ecall 進入 kernel
 * 使用 RISC-V syscall convention：
 * a7 = syscall number
 * a0 = 第一個參數
 * a1 = 第二個參數
 * a2 = 第三個參數
 * syscall return value = a0
 */
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

static long str_len(const char *s) {
    long len = 0;

    while (s[len])
        len++;

    return len;
}

static void user_puts(const char *s) {
    /*
     * a7 = 2    SYS_UART_WRITE
     * a0 = s    buffer address
     * a1 = len  length
     * a2 = 0
     * ecall
     */
    user_syscall(2, (long)s, str_len(s), 0);
}

static long user_getpid(void) {
    return user_syscall(0, 0, 0, 0);
}

static long user_fork_call(void) {
    return user_syscall(4, 0, 0, 0);
}

static long user_waitpid(long pid) {
    return user_syscall(5, pid, 0, 0);
}

static void user_exit(int status) {
    user_syscall(6, status, 0, 0);

    while (1)
        ;
}

static long user_exec(const char *path) {
    return user_syscall(3, (long)path, 0, 0);
}

static long user_stop(long pid) {
    return user_syscall(7, pid, 0, 0);
}

static void user_put_dec(long x) {
    char buf[32];
    int i = 0;

    if (x == 0) {
        user_puts("0");
        return;
    }

    if (x < 0) {
        user_puts("-");
        x = -x;
    }

    while (x > 0) {
        buf[i++] = '0' + (x % 10);
        x /= 10;
    }

    while (i > 0) {
        char c[2];
        c[0] = buf[--i];
        c[1] = 0;
        user_puts(c);
    }
}

void user_program(void) {
    user_puts("Hello from U-mode user program\n");

    long pid = user_getpid();

    user_puts("getpid = ");
    user_put_dec(pid);
    user_puts("\n");

    user_exit((int)pid);
}

void user_fork_test(void) {
    user_puts("Fork test start, pid = ");
    user_put_dec(user_getpid());
    user_puts("\n");

    long ret = user_fork_call();

    if (ret == 0) {
        user_puts("child: pid = ");
        user_put_dec(user_getpid());
        user_puts("\n");

        user_exit(0);
    } else if (ret > 0) {
        user_puts("parent: pid = ");
        user_put_dec(user_getpid());
        user_puts(", child pid = ");
        user_put_dec(ret);
        user_puts("\n");

        long waited = user_waitpid(ret);

        user_puts("parent: waitpid returned ");
        user_put_dec(waited);
        user_puts("\n");

        user_exit(0);
    } else {
        user_puts("fork failed\n");
        user_exit(-1);
    }
}

static void wait_task_done(struct task_struct *task) {
    while (task->state != TASK_ZOMBIE && task->state != TASK_UNUSED)
        schedule();

    if (task->state == TASK_ZOMBIE)
        task_reap(task);
}

void run_user_test(void) {
    uart_puts("create user process\n");

    struct task_struct *task = user_process_create(user_program);

    if (task == 0) {
        uart_puts("user_process_create failed\n");
        return;
    }

    wait_task_done(task);

    uart_puts("user process done\n");
}

void run_fork_test(void) {
    uart_puts("create fork test process\n");

    struct task_struct *task = user_process_create(user_fork_test);

    if (task == 0) {
        uart_puts("user_process_create failed\n");
        return;
    }

    wait_task_done(task);

    uart_puts("fork test done\n");
}

/*
 * 測試流程是：
 * 原本 create 來執行 exec 的 process1 變成執行 exec("forktest")
 * kernel 修改目前 process 的 trap frame
 * sret 後直接跳去 user_fork_test()
 */
void user_exec_test(void) {
    user_puts("Exec test start, pid = ");
    user_put_dec(user_getpid());
    user_puts("\n");

    user_puts("exec to forktest\n");

    long ret = user_exec("forktest");

    /*
     * 如果 exec 成功，理論上不會回到這裡。
     * 因為目前 process 的 trap frame 會被換成 user_fork_test。
     */
    user_puts("exec failed, ret = ");
    user_put_dec(ret);
    user_puts("\n");

    user_exit(-1);
}

void run_exec_test(void) {
    uart_puts("create exec test process\n");

    struct task_struct *task = user_process_create(user_exec_test);

    if (task == 0) {
        uart_puts("user_process_create failed\n");
        return;
    }

    wait_task_done(task);

    uart_puts("exec test done\n");
}

/*
 * 測試流程是：
 * parent fork child
 * parent 立刻 stop(child)
 * parent waitpid(child)
 * child 不應該真的執行
 */
void user_stop_test(void) {
    user_puts("Stop test start, pid = ");
    user_put_dec(user_getpid());
    user_puts("\n");

    long ret = user_fork_call();

    if (ret == 0) {
        /*
         * 正常情況下，parent 會在 child 被 schedule 前 stop child。
         * 如果 child 真的跑到這裡，代表 stop 太晚或 scheduler 行為不同。
         */
        user_puts("child: should be stopped, pid = ");
        user_put_dec(user_getpid());
        user_puts("\n");

        user_exit(0);
    } else if (ret > 0) {
        user_puts("parent: child pid = ");
        user_put_dec(ret);
        user_puts("\n");

        long stop_ret = user_stop(ret);

        user_puts("parent: stop returned ");
        user_put_dec(stop_ret);
        user_puts("\n");

        long waited = user_waitpid(ret);

        user_puts("parent: waitpid returned ");
        user_put_dec(waited);
        user_puts("\n");

        user_exit(0);
    } else {
        user_puts("fork failed\n");
        user_exit(-1);
    }
}

void run_stop_test(void) {
    uart_puts("create stop test process\n");

    struct task_struct *task = user_process_create(user_stop_test);

    if (task == 0) {
        uart_puts("user_process_create failed\n");
        return;
    }

    wait_task_done(task);

    uart_puts("stop test done\n");
}