#include "thread.h"
#include "trap.h"
#include "user.h"
#include "uart.h"

extern struct task_struct *alloc_task_for_user(void);
extern unsigned char user_stacks[MAX_THREADS][USER_STACK_SIZE];

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

void user_process_entry(void) {
    struct task_struct *current = get_current();

    /*
     * sscratch 存 kernel stack top。
     * 之後 user process 執行 user trap (ecall) 時，handle_exception 會用 sscratch 把 stack 換回 kernel stack
     */
    asm volatile("csrw sscratch, %0" : : "r"(current->kernel_sp) : "memory");

    return_to_user(&current->trapframe); // 用 sret 切到 U-mode 的 user_program()

    while (1)
        ;
}

struct task_struct *user_process_create(void (*entry)(void)) {
    struct task_struct *task = thread_create(user_process_entry); // 透過 user_process_entry 的 return_to_user 切回 U-mode 執行 user program

    if (task == 0)
        return 0;

    int idx = task_index(task);

    task->is_user = 1;
    task->parent = get_current();

    task->user_stack_base = (unsigned long)user_stacks[idx];
    task->user_stack_top = (unsigned long)&user_stacks[idx][USER_STACK_SIZE];
    task->user_stack_top &= ~0xFUL; // stack pointer 16-byte alignment

    /*
     * kernel_sp 是這個 task 的 kernel stack top。
     * 你如果用 static stacks[idx] 作 kernel stack，就可以這樣設。
     */
    task->kernel_sp = task->context.sp;

    struct trap_frame *tf = &task->trapframe;

    for (int i = 0; i < sizeof(struct trap_frame) / sizeof(unsigned long); i++)
        ((unsigned long *)tf)[i] = 0;

    tf->sepc = (unsigned long)entry;
    tf->sp = task->user_stack_top;
    tf->tp = (unsigned long)task;

    setup_user_sstatus(tf);

    return task;
}