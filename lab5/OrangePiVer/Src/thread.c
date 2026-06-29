#include "thread.h"
#include "uart.h"

static struct task_struct tasks[MAX_THREADS];
static unsigned char stacks[MAX_THREADS][STACK_SIZE] __attribute__((aligned(16)));
unsigned char user_stacks[MAX_THREADS][USER_STACK_SIZE] __attribute__((aligned(16)));

static struct task_struct *run_queue_tail = 0;
static struct task_struct *idle_task = 0;

static int next_pid = 1;

int task_index(struct task_struct *task) {
    return task - tasks;
}

static void runq_add(struct task_struct *task) {
    if (run_queue_tail == 0) {
        run_queue_tail = task;
        task->next = task;
        return;
    }

    task->next = run_queue_tail->next;
    run_queue_tail->next = task;
    run_queue_tail = task;
}

static void runq_remove(struct task_struct *task) {
    if (run_queue_tail == 0 || task == 0)
        return;

    if (task->next == task) {
        run_queue_tail = 0;
        task->next = 0;
        return;
    }

    struct task_struct *prev = run_queue_tail;

    while (prev->next != task) {
        prev = prev->next;

        if (prev == run_queue_tail)
            return;
    }

    prev->next = task->next;

    if (run_queue_tail == task)
        run_queue_tail = prev;

    task->next = 0;
}

static int is_runnable(struct task_struct *task) {
    return task && (task->state == TASK_RUNNABLE || task->state == TASK_RUNNING);
}

static struct task_struct *pick_next(struct task_struct *current) {
    struct task_struct *next = current->next;

    while (next != current) {
        if (is_runnable(next))
            return next;

        next = next->next;
    }

    return current;
}

static struct task_struct *alloc_task(void) {
    kill_zombies();

    for (int i = 0; i < MAX_THREADS; i++) {
        if (tasks[i].state == TASK_UNUSED)
            return &tasks[i];
    }

    return 0;
}

static void clear_context(struct cpu_context *ctx) {
    ctx->ra = 0;
    ctx->sp = 0;

    for (int i = 0; i < 12; i++)
        ctx->s[i] = 0;
}

static void thread_trampoline(void) {
    struct task_struct *current = get_current();

    if (current->entry)
        current->entry();

    thread_exit();
}

/*
 * 把目前 shell 包裝成 idle/current task
 */
void thread_init(void) {
    for (int i = 0; i < MAX_THREADS; i++) {
        tasks[i].pid = -1;
        tasks[i].state = TASK_UNUSED;
        tasks[i].entry = 0;
        tasks[i].stack_base = 0;
        tasks[i].next = 0;
        clear_context(&tasks[i].context); // clear ra, sp, s0-s11
        tasks[i].is_user = 0;
        tasks[i].exit_status = 0;
        tasks[i].kernel_sp = 0;
        tasks[i].user_stack_base = 0;
        tasks[i].user_stack_top = 0;
        tasks[i].parent = 0;
    }

    idle_task = &tasks[0];

    idle_task->pid = 0;
    idle_task->state = TASK_RUNNING;
    idle_task->entry = idle;
    idle_task->stack_base = (unsigned long)stacks[0];

    unsigned long sp = (unsigned long)&stacks[0][STACK_SIZE]; // stack grows downwards, 原本是 pointer, 但我們要對它做 bit operation, 比較適合轉成整數型別做
    sp &= ~0xFUL; // RISC-V calling convention 要求 stack pointer 16-byte alignment, 所以把 sp 的最低 4 bits 清零

    idle_task->context.ra = (unsigned long)idle;
    idle_task->context.sp = sp;

    runq_add(idle_task);

    asm volatile("mv tp, %0" : : "r"(idle_task) : "memory"); // memory clobber 是為了確保這行程式碼不會被編譯器優化掉
}

struct task_struct *thread_create(thread_fn_t fn) {
    struct task_struct *task = alloc_task(); // 找一個 TASK_UNUSED 的 task_struct

    if (task == 0)
        return 0;

    int idx = task - tasks;

    clear_context(&task->context);

    task->pid = next_pid++;
    task->state = TASK_RUNNABLE;
    task->entry = fn;
    task->stack_base = (unsigned long)stacks[idx];
    task->next = 0;

    unsigned long sp = (unsigned long)&stacks[idx][STACK_SIZE];
    sp &= ~0xFUL;

    task->context.ra = (unsigned long)thread_trampoline;
    task->context.sp = sp;

    runq_add(task);

    return task;
}

void schedule(void) {
    struct task_struct *prev = get_current();

    if (prev == 0 || run_queue_tail == 0) // 防呆, if schedule() before thread_init(), 理論上不應該發生
        return;

    struct task_struct *next = pick_next(prev);

    if (next == prev)
        return;

    if (prev->state == TASK_RUNNING)
        prev->state = TASK_RUNNABLE;

    next->state = TASK_RUNNING;

    switch_to(prev, next);
}

/*
 * 取得 current task
 * 選下一個要跑的 task
 * 把 current 從 run queue 移除
 * current->state = TASK_ZOMBIE
 * switch_to(current, next)
 */
void thread_exit(void) {
    struct task_struct *prev = get_current();

    if (prev == idle_task) {
        while (1)
            schedule();
    }

    struct task_struct *next = 0;

    if (prev->parent &&
        prev->parent->state != TASK_UNUSED &&
        prev->parent->state != TASK_ZOMBIE) {
        next = prev->parent;
    } else {
        next = pick_next(prev);
    }

    runq_remove(prev);

    prev->state = TASK_ZOMBIE;

    if (next == prev || next == 0)
        next = idle_task;

    next->state = TASK_RUNNING;

    switch_to(prev, next);

    while (1)
        ;
}

void kill_zombies(void) {
    for (int i = 1; i < MAX_THREADS; i++) {
        if (tasks[i].state != TASK_ZOMBIE)
            continue;

        /*
         * 如果 parent 是 user process，先不要由 idle 自動清。
         * 讓 parent 用 waitpid() 回收。
         */
        if (tasks[i].parent && tasks[i].parent->is_user &&
            tasks[i].parent->state != TASK_UNUSED &&
            tasks[i].parent->state != TASK_ZOMBIE) {
            continue;
        }

        task_reap(&tasks[i]);
    }
}

void idle(void) {
    while (1) {
        kill_zombies();
        schedule(); // 持續讓出 CPU 給其他 runnable thread
    }
}

/*
 * 根據 pid 找到對應的 task address inside the tasks[] array
 */
struct task_struct *task_find_by_pid(long pid) {
    for (int i = 0; i < MAX_THREADS; i++) {
        if (tasks[i].state != TASK_UNUSED && tasks[i].pid == pid)
            return &tasks[i];
    }

    return 0;
}

/*
 * 回收 zombie task, 把 child 的 task_struct 清乾淨
 */
void task_reap(struct task_struct *task) {
    if (task == 0)
        return;

    if (task == idle_task)
        return;

    if (task->state != TASK_ZOMBIE)
        return;

    task->pid = -1;
    task->state = TASK_UNUSED;
    task->entry = 0;
    task->stack_base = 0;
    task->next = 0;

    task->is_user = 0;
    task->exit_status = 0;
    task->kernel_sp = 0;
    task->user_stack_base = 0;
    task->user_stack_top = 0;
    task->parent = 0;

    clear_context(&task->context);

    unsigned long *p = (unsigned long *)&task->trapframe;
    for (int i = 0; i < sizeof(struct trap_frame) / sizeof(unsigned long); i++)
        p[i] = 0;
}

/*
 * 如果 task 還活著，就標成 TASK_ZOMBIE, 從 run queue 移除
 * waitpid(pid) 之後可以回收它
 * 如果 stop 自己，就直接走正常 thread_exit()
 */
int task_kill(long pid, int status) {
    struct task_struct *task = task_find_by_pid(pid);

    if (task == 0)
        return -1;

    if (task == idle_task)
        return -1;

    if (task->state == TASK_UNUSED || task->state == TASK_ZOMBIE)
        return -1;

    /*
     * 如果 stop 自己，就直接走正常 thread_exit()
     */
    if (task == get_current()) {
        task->exit_status = status;
        thread_exit();

        return 0;
    }

    /*
     * 目標不是目前正在跑的 task。
     * 單核心下它一定不在 CPU 上，直接從 run queue 移除並標成 zombie。
     */
    runq_remove(task);

    task->exit_status = status;
    task->state = TASK_ZOMBIE;

    return 0;
}