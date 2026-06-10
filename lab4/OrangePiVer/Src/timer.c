#include "timer.h"
#include "sbi.h"
#include "uart.h"
#include "irq.h"
#include "task.h"

#define TIMER_FREQ          24000000UL
#define BOOT_TICK_SEC       2
#define TIMER_POOL_SIZE     32
#define SIE_STIE (1UL << 5)

struct timer_event {
    unsigned long expires_at;
    timer_callback_t callback;
    void *arg;
    struct timer_event *next;
};

static struct timer_event timer_pool[TIMER_POOL_SIZE];
static struct timer_event *timer_free_list = 0; // 記錄目前哪些 timer_event 還沒被使用
static struct timer_event *timer_head = 0; // 指向最早到期的 timer event

static unsigned long boot_time_base = 0;

static inline void enable_stie(void) {
    asm volatile("csrs sie, %0" :: "r"(SIE_STIE));
}

static inline void disable_stie(void) {
    asm volatile("csrc sie, %0" :: "r"(SIE_STIE));
}

static inline void enable_sie(void) {
    asm volatile("csrs sstatus, %0" :: "r"(1UL << 1));
}

static inline unsigned long read_time(void) {
    unsigned long x;
    asm volatile("csrr %0, time" : "=r"(x));
    return x;
}

static struct timer_event *timer_alloc_node(void) { // 現在要執行的 event
    struct timer_event *n = timer_free_list;

    if (n) { // 從 free list 拿出一個 node，更新 free list 的 head
        timer_free_list = n->next; 
        n->next = 0; 
    }

    return n;
}

static void timer_free_node(struct timer_event *n) { // event 執行完了，把 node 放回 free list
    n->next = timer_free_list;
    timer_free_list = n;
}

static void timer_program_next_locked(void) { // 根據 timer_head 設定 hardware timer
    if (timer_head) {
        long err = sbi_set_timer(timer_head->expires_at); // 把硬體 timer 設成最早到期的 software timer

        if (err != 0) {
            uart_puts("[timer] sbi_set_timer error: ");
            uart_hex(err);
            uart_puts("\n");
        }

        enable_stie();
    } else {
        /*
         * 沒有任何 timer event。
         *
         * 必須停止 timer interrupt，不然目前 timer compare value
         * 可能已經過期，CPU 會一直進 timer interrupt。
         */
        sbi_set_timer(~0UL);
        disable_stie();
    }
}

/*
 * 為了保留 Basic Exercise 2 的 boot time 輸出。
 * 它本身也是一個 software timer。
 */
static void boot_tick_callback(void *arg) {
    (void)arg;

    uart_puts("boot time: ");
    uart_dec(timer_get_boot_time());
    uart_puts("\n");

    /*
     * 重新註冊自己，形成每 2 秒一次的週期性工作。
     */
    add_timer(boot_tick_callback, 0, BOOT_TICK_SEC);
}

void timer_init(void) {
    boot_time_base = read_time();

    timer_free_list = &timer_pool[0];

    for (int i = 0; i < TIMER_POOL_SIZE - 1; i++) {
        timer_pool[i].next = &timer_pool[i + 1];
    }

    timer_pool[TIMER_POOL_SIZE - 1].next = 0;
    timer_head = 0;

    // uart_puts("boot time: ");
    // uart_dec(0);
    // uart_puts("\n");

    /*
     * 把原本 Basic 2 的「每 2 秒印 boot time」
     * 也改成 software timer event。
     */
    // add_timer(boot_tick_callback, 0, BOOT_TICK_SEC);

    /*
     * 不註冊 boot_tick_callback。
     * 所以目前 timer queue 是空的。
     */

    /*
     * global S-mode interrupt 要開，因為 UART external interrupt 還需要它。
     */
    enable_sie();

    /*
     * 目前沒有 timer event，所以關掉 STIE。
     * 之後 add_timer() 插入第一個 timer 時會再打開。
     */
    timer_program_next_locked();
}
 
/*
 * 新增一個 software timer。
 *
 * callback：到期時要執行的 function
 * arg：傳給 callback 的參數
 * sec：幾秒後執行
 */
int add_timer(timer_callback_t callback, void *arg, int sec) {
    if (!callback || sec < 0) return -1;

    unsigned long s = local_irq_save();

    struct timer_event *n = timer_alloc_node();

    if (!n) {
        local_irq_restore(s);
        return -1;
    }

    unsigned long now = read_time();
    unsigned long delay_ticks = (unsigned long)sec * TIMER_FREQ;

    n->expires_at = now + delay_ticks;
    n->callback = callback;
    n->arg = arg;
    n->next = 0;

    /*
     * sorted insert。
     * timer_head 永遠指向最早到期的 timer。
     *
     * 如果 expires_at 相同，新的 event 會排在舊 event 後面，
     * 這樣可以維持先設定者先執行。
     */
    if (!timer_head || n->expires_at < timer_head->expires_at) { // 新增的 timer 比原本 head 更早到期, 插在前面
        n->next = timer_head;
        timer_head = n;

        timer_program_next_locked(); // 要重新設定 hardware timer

        local_irq_restore(s);
        return 0;
    }

    struct timer_event *cur = timer_head;

    while (cur->next && cur->next->expires_at <= n->expires_at) { // 找到位置並插在屁股
        cur = cur->next;
    }
    n->next = cur->next;
    cur->next = n;

    local_irq_restore(s);
    return 0;
}

/*
 * trap.c 收到 supervisor timer interrupt 後會呼叫這個 function。
 */
void timer_handle_interrupt(void) {
    while (1) {
        unsigned long s = local_irq_save();

        unsigned long now = read_time();

        if (!timer_head || timer_head->expires_at > now) {
            /*
            * 如果 queue 空了：
            *     timer_program_next_locked() 會 disable STIE
            *
            * 如果還有下一個 timer：
            *     timer_program_next_locked() 會設下一次 timer
            */
            timer_program_next_locked();
            local_irq_restore(s);
            return;
        }

        /*
         * 取出最早到期的 timer。
         * 已經執行完的 event 可以依依放回 free list 了
         */
        struct timer_event *n = timer_head;
        timer_head = timer_head->next;

        timer_callback_t callback = n->callback;
        void *arg = n->arg;

        timer_free_node(n);

        local_irq_restore(s);

        /*
         * timer interrupt handler 不直接執行 callback，
         * 而是把 callback 包成 task。
         */
        if (callback) {
            if (add_task((task_callback_t)callback, arg, 1) < 0) {
                /*
                 * 如果 task queue 滿了，就 fallback 直接執行，
                 * 避免 timeout callback 完全遺失。
                 */
                callback(arg);
            }
        }     
    }
}

unsigned long timer_get_boot_time(void) {
    unsigned long now = read_time();
    return (now - boot_time_base) / TIMER_FREQ;
}

void timer_trigger_now(void) {
    unsigned long s = local_irq_save();

    /*
     * 把 timer 設成現在，強制很快進一次 timer interrupt
     * 測試 nested interrupt / preemption。
     */
    long err = sbi_set_timer(read_time());

    if (err != 0) {
        uart_puts("[timer] trigger_now error: ");
        uart_hex(err);
        uart_puts("\n");
    }

    enable_stie();

    local_irq_restore(s);
}