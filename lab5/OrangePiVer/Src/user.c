#include "thread.h"
#include "trap.h"
#include "user.h"
#include "uart.h"

extern void user_program(void);
extern void user_fork_test(void);
extern void user_stop_test(void);
extern void user_exec_test(void);

static void mem_zero(void *dst, unsigned long n) {
    unsigned char *d = (unsigned char *)dst;

    for (unsigned long i = 0; i < n; i++)
        d[i] = 0;
}

static void mem_copy(void *dst, const void *src, unsigned long n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;

    for (unsigned long i = 0; i < n; i++)
        d[i] = s[i];
}

static int str_eq(const char *a, const char *b) {
    while (*a && *b) {
        if (*a != *b)
            return 0;

        a++;
        b++;
    }

    return *a == 0 && *b == 0;
}

static void setup_user_sstatus(struct trap_frame *tf) {
    unsigned long sstatus;

    asm volatile("csrr %0, sstatus" : "=r"(sstatus));

    /*
     * SPP = 0：sret 後回 U-mode
     * SPIE = 1：sret 後允許 interrupt
     */
    sstatus &= ~(1UL << 8);   // clear SPP
    sstatus |=  (1UL << 5);   // set SPIE

    tf->sstatus = sstatus;
}

static void setup_user_trapframe(struct task_struct *task, struct trap_frame *tf, void (*entry)(void)) {
    mem_zero(tf, sizeof(struct trap_frame));

    tf->sepc = (unsigned long)entry;
    tf->sp = task->user_stack_top;

    /*
     * 目前你的 kernel 用 tp 當 current task pointer。
     * user mode 回來 trap 時，handle_exception 會保存 tp，
     * do_trap() 裡的 get_current() 也依賴 tp 還是 current task。
     */
    tf->tp = (unsigned long)task;

    setup_user_sstatus(tf);
}

static void *resolve_user_program(const char *path) {
    if (path == 0)
        return 0;

    if (str_eq(path, "usertest"))
        return user_program;

    if (str_eq(path, "forktest"))
        return user_fork_test;

    if (str_eq(path, "stoptest"))
        return user_stop_test;

    if (str_eq(path, "exectest"))
        return user_exec_test;

    return 0;
}

/*
 * thread 在 S-mode 執行的 entry_point, 執行 sret 之後真正變成 user process
 */
void user_process_entry(void) { 
    struct task_struct *current = get_current();

    /*
     * sscratch 存 kernel stack top。
     * 之後 user process 執行 user trap (ecall) 時，handle_exception 會用 sscratch 把 stack 換回 kernel stack
     */
    asm volatile("csrw sscratch, %0" : : "r"(current->kernel_sp) : "memory");

    return_to_user(&current->trapframe); // 載入 reg/sepc/sstatus, sret 切到 U-mode 後, 開始執行 trapframe.sepc 指向的 user program


    while (1)
        ;
}


struct task_struct *user_process_create(void (*entry)(void)) {
    struct task_struct *task = thread_create(user_process_entry);// 透過 user_process_entry 的 return_to_user 切回 U-mode 執行 user program

    if (task == 0)
        return 0;

    int idx = task_index(task);

    task->is_user = 1;
    task->exit_status = 0;
    task->parent = get_current();

    task->user_stack_base = (unsigned long)user_stacks[idx];
    task->user_stack_top = (unsigned long)&user_stacks[idx][USER_STACK_SIZE];
    task->user_stack_top &= ~0xFUL; // stack pointer 16-byte alignment

    /*
     * kernel_sp 是這個 task 的 kernel stack top。
     * 你如果用 static stacks[idx] 作 kernel stack，就可以這樣設。
     */
    task->kernel_sp = task->context.sp;

    mem_zero((void *)task->user_stack_base, USER_STACK_SIZE);

    setup_user_trapframe(task, &task->trapframe, entry);

    return task;
}

static unsigned long translate_stack_ptr(struct task_struct *parent, struct task_struct *child, unsigned long ptr) {
    if (ptr >= parent->user_stack_base && ptr <= parent->user_stack_top) {
        unsigned long offset = ptr - parent->user_stack_base;
        return child->user_stack_base + offset;
    }

    return ptr;
}

long user_fork(struct trap_frame *parent_tf) {
    struct task_struct *parent = get_current();

    if (parent == 0 || !parent->is_user)
        return -1;

    struct task_struct *child = thread_create(user_process_entry);

    if (child == 0)
        return -1;

    int child_idx = task_index(child);

    child->is_user = 1;
    child->exit_status = 0;

    child->user_stack_base = (unsigned long)user_stacks[child_idx];
    child->user_stack_top = (unsigned long)&user_stacks[child_idx][USER_STACK_SIZE];
    child->user_stack_top &= ~0xFUL;

    child->kernel_sp = child->context.sp;

    /*
     * 複製整個 user stack。
     * 目前沒有 MMU，這是最直觀的 fork 模型。
     */
    mem_copy((void *)child->user_stack_base, (void *)parent->user_stack_base, USER_STACK_SIZE);

    /*
     * 複製 parent 當下的 user register context。
     * parent_tf->sepc 在 do_trap 裡已經 +4，
     * 所以 child 回去後也會從 fork() 的下一行開始。
     * 
     * 不要用 struct assignment, 因為 bare-metal 沒有 libc，
     * compiler 可能產生 memcpy 呼叫，導致 link error
     * e.g. child->trapframe = *parent_tf;
     */
    mem_copy(&child->trapframe, parent_tf, sizeof(struct trap_frame));

    /*
     * fork convention:
     * parent return child pid
     * child return 0
     */
    child->trapframe.a0 = 0;

    /*
     * child 的 current task pointer 要改成 child。
     */
    child->trapframe.tp = (unsigned long)child;

    /*
     * child user stack 是另一塊 memory。
     * sp 一定要轉換到 child stack 對應位置。
     */
    child->trapframe.sp = translate_stack_ptr(parent, child, parent_tf->sp);

    /*
     * s0 在 RISC-V 常被當 frame pointer。
     * 如果它指向 parent user stack，也要轉到 child stack。
     */
    child->trapframe.s0 = translate_stack_ptr(parent, child, parent_tf->s0);

    child->parent = parent;

    return child->pid;
}

int user_exec_current(const char *path, struct trap_frame *tf) {
    struct task_struct *current = get_current();

    if (current == 0 || !current->is_user)
        return -1;

    void (*entry)(void) = resolve_user_program(path);

    if (entry == 0)
        return -1;

    int idx = task_index(current);

    current->user_stack_base = (unsigned long)user_stacks[idx];
    current->user_stack_top = (unsigned long)&user_stacks[idx][USER_STACK_SIZE];
    current->user_stack_top &= ~0xFUL;

    mem_zero((void *)current->user_stack_base, USER_STACK_SIZE);

    /*
     * exec 是取代目前 process 的 user context。
     * 這裡直接改目前 trap frame。
     * 讓程式不回到原本 ecall 的下一行位置。
     * syscall return 後，handle_exception 會從這個 tf restore，
     * 因此 sret 會跳到新的 entry。
     */
    setup_user_trapframe(current, tf, entry);

    /*
     * exec success return 0。
     * do_trap() 後面還會把 syscall_handler return value 寫回 tf->a0。
     */
    return 0;
}