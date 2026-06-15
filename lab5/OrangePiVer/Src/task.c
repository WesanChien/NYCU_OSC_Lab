#include "task.h"
#include "timer.h"
#include "uart.h"
#include "irq.h"

#define TASK_POOL_SIZE 64

struct task_event {
    int priority; // priority 越大越優先
    task_callback_t callback; // function pointer，放 task 要執行的 function address
    void *arg; // 傳給 callback 的參數
    struct task_event *next;
};

static struct task_event task_pool[TASK_POOL_SIZE];
static struct task_event *task_free_list = 0;
static struct task_event *task_head = 0;

static int task_current_priority = -2147483647; // INT_MIN + 1, 代表目前沒有 task 在跑

static inline void enable_sie(void) {
    asm volatile("csrs sstatus, %0" :: "r"(1UL << 1));
}

static inline void disable_sie(void) {
    asm volatile("csrc sstatus, %0" :: "r"(1UL << 1));
}

static struct task_event *task_alloc_node(void) { // 從 free list 拿出一個 node
    struct task_event *n = task_free_list;

    if (n) {
        task_free_list = n->next;
        n->next = 0;
    }

    return n;
}

static void task_free_node(struct task_event *n) {
    n->next = task_free_list;
    task_free_list = n;
}

void task_init(void) {
    task_free_list = &task_pool[0];

    for (int i = 0; i < TASK_POOL_SIZE - 1; i++) {
        task_pool[i].next = &task_pool[i + 1];
    }

    task_pool[TASK_POOL_SIZE - 1].next = 0;

    task_head = 0;
    task_current_priority = -2147483647;
}

/*
 * task queue 會依照 priority 由大到小排序。
 */
int add_task(task_callback_t callback, void *arg, int priority) {
    if (!callback) return -1;

    unsigned long s = local_irq_save();

    struct task_event *n = task_alloc_node();

    if (!n) {
        local_irq_restore(s);
        return -1;
    }

    n->priority = priority;
    n->callback = callback;
    n->arg = arg;
    n->next = 0;

    /*
     * 如果 queue 空，或新 task priority 比 head 高，插到最前面。
     */
    if (!task_head || priority > task_head->priority) {
        n->next = task_head;
        task_head = n;
        local_irq_restore(s);
        return 0;
    }

    /*
     * 否則找到第一個 priority 比自己低的位置插入。
     *
     * 如果 priority 相同，排在同 priority task 後面，
     * 讓同優先權接近 FIFO。
     */
    struct task_event *cur = task_head;

    while (cur->next && cur->next->priority >= priority) {
        cur = cur->next;
    }

    n->next = cur->next;
    cur->next = n;

    local_irq_restore(s);
    return 0;
}

/*
 * 會在 do_trap() 最後被呼叫執行 pending tasks(task_head)
 * 執行 task_queue 的 task_head，所以 Preempt 進來的 high priority task 會被優先執行
 */
void run_pending_task(void) {
    while (1) {
        unsigned long s = local_irq_save();

        if (!task_head) {
            local_irq_restore(s);
            return;
        }

        if (task_current_priority > task_head->priority) { // Preemption 判斷
            local_irq_restore(s);
            return;
        }

        struct task_event *n = task_head;
        task_head = task_head->next;

        task_callback_t callback = n->callback;
        void *arg = n->arg;
        int priority = n->priority;

        task_free_node(n);

        int prev_priority = task_current_priority;
        task_current_priority = priority;

        local_irq_restore(s);

        /*
         * Nested interrupt 的重點：
         *
         * task 執行時開啟 S-mode interrupt(trap 進來時，RISC-V 會把 S-mode interrupt 關掉)
         * 這樣 task 執行到一半時，timer / UART interrupt 仍可進來。
         */
        enable_sie();

        callback(arg);

        /*
         * callback 結束後，我們還在 trap handler 的上下文裡。
         * 接下來要恢復 task_current_priority、回到 do_trap()、最後由 start.S restore trap frame。
         * 所以先關回 interrupt，避免在整理 task 狀態時又被打斷
         */
        disable_sie();

        s = local_irq_save();
        task_current_priority = prev_priority;
        local_irq_restore(s);
    }
}
static int adv2_priority_set[4];

static void adv2_p1_callback(void *arg) {
    (void)arg;

    uart_puts("P1 start\n");
    uart_puts("P1 end\n");
}

static void adv2_p3_callback(void *arg) {
    (void)arg;

    uart_puts("P3 start\n");

    /*
     * P3 執行中新增 P1。
     * P1 priority 比 P3 高，所以應該 preempt P3。
     */
    add_task(adv2_p1_callback, 0, adv2_priority_set[0]);
    timer_trigger_now();

    uart_puts("P3 end\n");
}

static void adv2_p2_callback(void *arg) {
    (void)arg;

    uart_puts("P2 start\n");

    /*
     * P2 執行中新增 P3。
     * P3 priority 比 P2 低，所以不應該 preempt P2。
     */
    add_task(adv2_p3_callback, 0, adv2_priority_set[2]);
    timer_trigger_now(); // 強制產生一次 timer interrupt，CPU 進入 nested trap，然後 run_pending_task() 發現 P2 比 P4 優先，就開始跑 P2。

    uart_puts("P2 end\n");
}

static void adv2_p4_callback(void *arg) {
    (void)arg;

    uart_puts("P4 start\n");

    /*
     * P4 執行中新增 P2。
     * P2 priority 比 P4 高，所以應該 preempt P4。
     */
    add_task(adv2_p2_callback, 0, adv2_priority_set[1]);
    timer_trigger_now();

    uart_puts("P4 end\n");
}

static void adv2_test_start(void *arg) {
    (void)arg;

    /*
     * 數字越大，priority 越高。
     *
     * P1 = 40
     * P2 = 30
     * P3 = 20
     * P4 = 10
     */
    adv2_priority_set[0] = 40;
    adv2_priority_set[1] = 30;
    adv2_priority_set[2] = 20;
    adv2_priority_set[3] = 10;

    add_task(adv2_p4_callback, 0, adv2_priority_set[3]);
}

/*
 * shell command 會呼叫這個。
 */
void task_queue_adv2_test(void) {
    /*
     * 用 timer 啟動測試，讓測試從 trap/task context 開始。
     */
    add_timer(adv2_test_start, 0, 0);
}