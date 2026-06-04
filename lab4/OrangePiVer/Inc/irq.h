#ifndef IRQ_H
#define IRQ_H

static inline unsigned long local_irq_save(void) {
    unsigned long s;
    asm volatile("csrr %0, sstatus" : "=r"(s)); // 讀出目前 sstatus 的值(舊值)到 s 裡
    asm volatile("csrci sstatus, 2"); // clear sstatus 的 bit 1 (SIE)，關掉 S-mode interrupt
    return s;
}

static inline void local_irq_restore(unsigned long s) {
    if (s & 2UL)
        asm volatile("csrsi sstatus, 2"); // sstatus |= 1U << 2, 原本舊狀態開啟就開啟回去
    else
        asm volatile("csrci sstatus, 2"); // sstatus &= ~(1U << 2), 原本舊狀態關閉就關閉回去
}

#endif