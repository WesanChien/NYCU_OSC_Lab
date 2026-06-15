#include "thread.h"
#include "uart.h"

static struct task_struct tasks[MAX_THREADS];
static unsigned char stacks[MAX_THREADS][STACK_SIZE] __attribute__((aligned(16)));

static struct task_struct *run_queue_tail = 0;
static struct task_struct *idle_task = 0;

static int next_pid = 1;

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

void thread_exit(void) {
    struct task_struct *prev = get_current();

    if (prev == idle_task) {
        while (1)
            schedule();
    }

    struct task_struct *next = pick_next(prev);

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
        if (tasks[i].state == TASK_ZOMBIE) {
            tasks[i].pid = -1;
            tasks[i].state = TASK_UNUSED;
            tasks[i].entry = 0;
            tasks[i].stack_base = 0;
            tasks[i].next = 0;
            clear_context(&tasks[i].context);
        }
    }
}

void idle(void) {
    while (1) {
        kill_zombies();
        schedule(); // 持續讓出 CPU 給其他 runnable thread
    }
}