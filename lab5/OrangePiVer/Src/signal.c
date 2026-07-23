#include "signal.h"
#include "syscall.h"
#include "user_addr.h"
#include "uart.h"
#include "thread.h"

static unsigned char signal_stacks[MAX_THREADS][SIGNAL_STACK_SIZE] __attribute__((aligned(16))); //  handler 真正開始執行時，提供給 handler 使用的 user stack

static void mem_zero_local(void *dst, unsigned long n) {
    unsigned char *d = (unsigned char *)dst;

    for (unsigned long i = 0; i < n; i++)
        d[i] = 0;
}

static void mem_copy_local(void *dst, const void *src, unsigned long n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;

    for (unsigned long i = 0; i < n; i++)
        d[i] = s[i];
}

static int valid_signal(int signum) {
    return signum > 0 && signum < MAX_SIGNALS;
}

static unsigned long signal_bit(int signum) {
    return 1UL << signum;
}

void signal_task_init(struct task_struct *task) {
    if (task == 0)
        return;

    for (int i = 0; i < MAX_SIGNALS; i++)
        task->signal_handlers[i] = 0;

    task->pending_signals = 0;
    task->handling_signal = 0;
    task->current_signal = 0;

    mem_zero_local(&task->signal_saved_tf, sizeof(struct trap_frame));
}

void signal_fork(struct task_struct *parent, struct task_struct *child) {
    if (parent == 0 || child == 0)
        return;

    /*
     * signal handler 要繼承。
     * 課程測試是：signal 之後 fork，child 要繼承 handler。
     */
    for (int i = 0; i < MAX_SIGNALS; i++)
        child->signal_handlers[i] = parent->signal_handlers[i];

    /*
     * pending signal / 正在處理 signal 的狀態不要繼承。
     */
    child->pending_signals = 0;
    child->handling_signal = 0;
    child->current_signal = 0;

    mem_zero_local(&child->signal_saved_tf, sizeof(struct trap_frame));
}

/*
 * 在 USER_SIGTRAMP_BASE 放一段 user-mode trampoline：
 *
 *     li a7, SYS_SIGRETURN
 *     ecall
 *     j .
 *
 * handler return 後會 ret 到這裡，
 * 然後自動呼叫 sigreturn syscall。
 */
void signal_install_trampoline(void) {
    unsigned int *code = (unsigned int *)USER_SIGTRAMP_BASE;

    /*
     * addi a7, zero, 11
     * ecall
     * jal zero, 0
     */
    code[0] = 0x00b00893;
    code[1] = 0x00000073;
    code[2] = 0x0000006f;

    asm volatile("fence.i" ::: "memory");
}

long sys_signal(int signum, unsigned long handler) {
    struct task_struct *current = get_current();

    if (current == 0 || !current->is_user)
        return -1;

    if (!valid_signal(signum))
        return -1;

    unsigned long old = current->signal_handlers[signum];

    current->signal_handlers[signum] = handler; // signal_handlers[15] = SIGTERM handler address

    return old; // The return value is the previous handler for the signal, you can ignore it in this lab.
}

long sys_kill(long pid, int signum) {
    if (!valid_signal(signum))
        return -1;

    struct task_struct *task = task_find_by_pid(pid);

    if (task == 0)
        return -1;

    if (!task->is_user)
        return -1;

    if (task->state == TASK_UNUSED || task->state == TASK_ZOMBIE)
        return -1;

    /*
     * 如果沒有註冊 user handler，採用 default action：
     * terminate process。
     */
    if (task->signal_handlers[signum] == 0) {
        return task_kill(pid, -signum);
    }

    /*
     * 有 handler，不直接終止。
     * 將 signal 標記為等待送達。
     * 不在這裡執行 handler，因為現在正在執行 kill() 的通常是 parent，
     * 而 handler 必須在 target child 的 context 中執行
     */
    task->pending_signals |= signal_bit(signum); // |= 1UL << 15, pending_signals = 0000 0000 0000 0000 1000 0000 0000 0000

    return 0;
}

static int find_pending_signal(struct task_struct *task) {
    for (int signum = 1; signum < MAX_SIGNALS; signum++) {
        if (task->pending_signals & signal_bit(signum))
            return signum;
    }

    return 0;
}

/*
 * 真正遞送 pending signal。
 * 它會在 target process 準備回 U-mode 前被呼叫
 */
void signal_try_deliver(struct trap_frame *tf) {
    struct task_struct *current = get_current();

    if (current == 0 || tf == 0)
        return;

    if (!current->is_user)
        return;

    /*
     * 不處理 nested signal。
     * 如果正在處理 signal，就不再送入第二個 signal。
     */
    if (current->handling_signal)
        return;

    int signum = find_pending_signal(current);

    if (signum == 0)
        return;

    unsigned long handler = current->signal_handlers[signum];

    if (handler == 0) {
        task_kill(current->pid, -signum);
        return;
    }

    /*
     * 清掉 pending bit。
     * 代表該 signal 已經開始 deliver，不再是等待狀態
     */
    current->pending_signals &= ~signal_bit(signum);

    /*
     * 保存原本 user context(Trap frame)。
     * sigreturn 時會恢復這份 trapframe。
     */
    mem_copy_local(&current->signal_saved_tf, tf, sizeof(struct trap_frame));

    current->handling_signal = 1;
    current->current_signal = signum;

    int idx = task_index(current);

    unsigned long sig_sp = (unsigned long)&signal_stacks[idx][SIGNAL_STACK_SIZE];

    sig_sp &= ~0xFUL;

    /*
     * 改寫即將 return to user 的 trapframe。
     *
     * sret 後不再回原本 user code，
     * 而是先跳到 signal handler。
     */
    tf->sepc = handler;

    /*
     * handler return 時會 ret 到 trampoline。
     * trampoline 會呼叫 sigreturn syscall。
     */
    tf->ra = USER_SIGTRAMP_BASE;

    /*
     * signal handler 使用獨立 user stack。
     */
    tf->sp = sig_sp;

    /*
     * handler prototype 課程寫 void (*handler)()。
     * 這裡放 signum 到 a0，即使 handler 不吃參數也不影響。
     */
    tf->a0 = signum;
}

long sys_sigreturn(struct trap_frame *tf) {
    struct task_struct *current = get_current();

    if (current == 0 || !current->is_user)
        return -1;

    if (!current->handling_signal)
        return -1;

    uart_puts("[sigreturn] signal handler finished\n");

    /*
     * 恢復 signal 發生前的 user context。
     */
    mem_copy_local(tf, &current->signal_saved_tf, sizeof(struct trap_frame));

    current->handling_signal = 0;
    current->current_signal = 0;

    mem_zero_local(&current->signal_saved_tf, sizeof(struct trap_frame));

    return 0;
}