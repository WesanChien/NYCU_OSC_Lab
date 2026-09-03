#include "plic.h"
#include "vm.h"

#define PLIC_BASE_PA 0xE0000000UL

static unsigned long plic_base;

// #define PLIC_BASE              0xE0000000UL // PLIC 的 MMIO base address

#define PLIC_PRIORITY_BASE     0x000000UL
#define PLIC_S_ENABLE_BASE     0x002080UL
#define PLIC_S_THRESHOLD_BASE  0x201000UL
#define PLIC_S_CLAIM_BASE      0x201004UL

#define UART0_IRQ_ID           42U

static unsigned long boot_hart_id = 0;

/*
 * MMIO 讀寫 helper functions
 */
static inline void write32(unsigned long addr, unsigned int val) {
    *(volatile unsigned int*)addr = val; // 指向 addr 的 unsigned int pointer 的值變成 val, reg 值可能自己改變, 所以加 volatile
}

static inline unsigned int read32(unsigned long addr) {
    return *(volatile unsigned int*)addr;
}

static inline unsigned long plic_priority_addr(unsigned int irq) {
    return plic_base + PLIC_PRIORITY_BASE + irq * 4;
}

static inline unsigned long plic_s_enable_addr(unsigned long hart_id, unsigned int irq) {
    /*
     * 一個 PLIC enable register, enable word 管 32 個 IRQ。
     * UART0 IRQ = 42，所以在 word 1，bit = 42 % 32 = 10。
     */
    return plic_base + PLIC_S_ENABLE_BASE + hart_id * 0x80UL + (irq / 32U) * 4UL;
}

static inline unsigned long plic_s_threshold_addr(unsigned long hart_id) {
    return plic_base + PLIC_S_THRESHOLD_BASE + hart_id * 0x2000UL;
}

static inline unsigned long plic_s_claim_addr(unsigned long hart_id) {
    return plic_base + PLIC_S_CLAIM_BASE + hart_id * 0x2000UL;
}

void plic_init(unsigned long hart_id) {
    plic_base = phys_to_virt_addr(PLIC_BASE_PA);

    boot_hart_id = hart_id;

    /*
     * 1. 設 UART IRQ priority。
     * priority 必須大於 threshold 才會被送出。
     */
    write32(plic_priority_addr(UART0_IRQ_ID), 1); // 把 UART0 IRQ 42 的 priority 設成 1(最低是 0，代表不送出)

    /*
     * 2. enable UART IRQ for this S-mode hart context。
     */
    unsigned long en_addr = plic_s_enable_addr(boot_hart_id, UART0_IRQ_ID);
    unsigned int en = read32(en_addr);
    en |= (1U << (UART0_IRQ_ID % 32U)); // 把 bit 10 設成 1
    write32(en_addr, en);

    /*
     * 3. threshold = 0，允許 priority >= 1 的 interrupt。
     */
    write32(plic_s_threshold_addr(boot_hart_id), 0);
}

/*
 * 問 PLIC 現在是哪一個 IRQ 觸發？
 * @return The ID of the claimed interrupt.
 */
unsigned int plic_claim(void) {
    return read32(plic_s_claim_addr(boot_hart_id));
}

/*
 * 告訴 PLIC 已經處理完 IRQ，可以接受下一個了
 */
void plic_complete(unsigned int irq) {
    write32(plic_s_claim_addr(boot_hart_id), irq);
}

void enable_external_interrupt(void) {
    /*
     * sie.SEIE = bit 9
     * enable supervisor external interrupt.
     */
    asm volatile("csrs sie, %0" :: "r"(1UL << 9));

    /*
     * sstatus.SIE = bit 1
     * enable global supervisor interrupt.
     */
    asm volatile("csrs sstatus, %0" :: "r"(1UL << 1));
}