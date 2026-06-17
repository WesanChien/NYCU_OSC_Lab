#ifndef THREAD_H
#define THREAD_H

#include "trap.h"

#define MAX_THREADS 16
#define STACK_SIZE  16384
#define USER_STACK_SIZE 16384

typedef void (*thread_fn_t)(void);

enum task_state {
    TASK_UNUSED = 0,
    TASK_RUNNABLE,
    TASK_RUNNING,
    TASK_ZOMBIE,
};

struct cpu_context {
    unsigned long ra;
    unsigned long sp;
    unsigned long s[12];
};

struct task_struct {
    /*
     * cpu_context 必須放在 struct 最前面。
     * 因 switch_to.S 假設：
     *   offset 0   = ra
     *   offset 8   = sp
     *   offset 16  = s0
     *   ...
     */
    struct cpu_context context; // thread 被切走時，要保存哪些 CPU registers

    int pid;
    enum task_state state; // 例如 RUNNABLE、RUNNING、ZOMBIE

    thread_fn_t entry; // thread 一開始要執行的 function

    unsigned long stack_base; // thread 的 stack 起點（stack grows downwards）
    struct task_struct *next;

    int is_user; // 區分 kernel thread / user process
    int exit_status; // kernel 需要記住它是用什麼 status 結束

    struct trap_frame trapframe; // 保存 user process 在 user-mode 的 register 狀態

    unsigned long kernel_sp; // user process trap 進 kernel 時要切到哪個 kernel stack top
    unsigned long user_stack_base;
    unsigned long user_stack_top;

    struct task_struct *parent;
};

static inline struct task_struct *get_current(void) {
    struct task_struct *current;
    asm volatile("mv %0, tp" : "=r"(current));
    return current;
}

void thread_init(void);
struct task_struct *thread_create(thread_fn_t fn);
void schedule(void);
void thread_exit(void);
void kill_zombies(void);
void idle(void);

void switch_to(struct task_struct *prev, struct task_struct *next);

int task_index(struct task_struct *task);
extern unsigned char user_stacks[MAX_THREADS][USER_STACK_SIZE];

#endif