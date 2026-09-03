#include "vm.h"
#include "mm.h"
#include "uart.h"

#define EARLY_MAP_GIB 10

/*
 * Root page table。
 *
 * Sv39 root table 必須 4KB aligned。
 */
static unsigned long early_pgd[SV39_PT_ENTRIES]
    __attribute__((aligned(4096)));

/*
 * 每一個 PMD table 可以映射：
 *
 *   512 entries × 2 MiB = 1 GiB
 *
 * 因此四個 PMD table 可映射 4 GiB。
 */
static unsigned long early_pmd[EARLY_MAP_GIB][SV39_PT_ENTRIES]
    __attribute__((aligned(4096)));

/*
 * Early boot 階段只需要幾張 PTE table
 * 來把特定 2 MiB MMIO window 拆成 4 KiB pages。
 */
#define EARLY_PTE_TABLES  4

static unsigned long early_pte[EARLY_PTE_TABLES][SV39_PT_ENTRIES]
    __attribute__((aligned(4096)));

static unsigned long early_pte_used;

static void clear_table(unsigned long *table) {
    for (unsigned long i = 0; i < SV39_PT_ENTRIES; i++)
        table[i] = 0;
}

static void map_mmio_2m_window(unsigned long pa)
{
    /*
     * 一張 PTE table 對應一個完整 2 MiB PMD range。
     */
    unsigned long base =
        pa & ~(SV39_PMD_SIZE - 1);

    /*
     * physical address 的 PGD / PMD index。
     */
    unsigned long pgd_index =
        (base >> SV39_PGD_SHIFT) & 0x1ffUL;

    unsigned long pmd_index =
        (base >> SV39_PMD_SHIFT) & 0x1ffUL;

    if (pgd_index >= EARLY_MAP_GIB)
        return;

    if (early_pte_used >= EARLY_PTE_TABLES)
        return;

    unsigned long *pte =
        early_pte[early_pte_used++];

    clear_table(pte);

    /*
     * 把整個 2 MiB window 拆成：
     *
     *   512 × 4 KiB
     */
    for (unsigned long i = 0;
         i < SV39_PT_ENTRIES;
         i++) {

        unsigned long page_pa =
            base + i * SV39_PAGE_SIZE;

        pte[i] =
            PA_TO_PTE(page_pa) |
            MMIO_PAGE_FLAGS;
    }

    /*
     * setup_vm() 執行時 MMU 還沒開，
     * 因此這裡取得的 runtime address 可當 PA。
     */
    unsigned long pte_pa =
        (unsigned long)pte;

    /*
     * 原本：
     *
     * PMD → 2 MiB leaf
     *
     * 現在改成：
     *
     * PMD → PTE table
     *
     * non-leaf entry 只能放 V，不能放 R/W/X。
     */
    early_pmd[pgd_index][pmd_index] =
        PA_TO_PTE(pte_pa) | PTE_V;
}

void setup_vm(void) {
    clear_table(early_pgd);

    for (unsigned long gib = 0; gib < EARLY_MAP_GIB; gib++)
        clear_table(early_pmd[gib]);

    /*
     * KERNEL_VA_OFFSET 的 VPN[2] 是 256。
     *
     * 因此：
     *
     * physical 0~1 GiB:
     *   identity PGD index = 0
     *   higher PGD index   = 256
     *
     * physical 1~2 GiB:
     *   identity PGD index = 1
     *   higher PGD index   = 257
     *
     * 依此類推。
     */
    const unsigned long kernel_pgd_base =
        (KERNEL_VA_OFFSET >> SV39_PGD_SHIFT) & 0x1ffUL;

    for (unsigned long gib = 0; gib < EARLY_MAP_GIB; gib++) {
        /*
         * setup_vm() 此時仍在 MMU 關閉狀態下執行。
         *
         * 由於程式使用 PC-relative addressing，
         * 這裡取得的是 early_pmd 的 runtime physical address。
         */
        unsigned long pmd_pa = (unsigned long)&early_pmd[gib][0];

        unsigned long non_leaf_entry = PA_TO_PTE(pmd_pa) | PTE_V;

        /*
         * Temporary identity mapping。
         */
        early_pgd[gib] = non_leaf_entry;

        /*
         * Permanent higher-half mapping。
         *
         * identity mapping 和 higher-half mapping 可以共用同一份
         * PMD，因為 PMD leaf 儲存的是目標 physical address。
         */
        early_pgd[kernel_pgd_base + gib] = non_leaf_entry;

        for (unsigned long i = 0; i < SV39_PT_ENTRIES; i++) {
            unsigned long pa = gib * SV39_PGD_SIZE + i * SV39_PMD_SIZE;

            /*
             * PMD-level leaf：
             * 一個 entry 直接映射 2 MiB physical memory。
             */
            early_pmd[gib][i] = PA_TO_PTE(pa) | KERNEL_PAGE_FLAGS;
        }
    }

    early_pte_used = 0;

    /*
     * K1 AP peripheral region。
     *
     * UART 0xd4017000 就位於這裡。
     */
    map_mmio_2m_window(0xd4000000UL);
    map_mmio_2m_window(0xd4200000UL);

    /*
     * 之後 timer 會用到 0xe0030000，
     * 所以先把包含它的 2 MiB window 改成 MMIO。
     */
    map_mmio_2m_window(0xe0000000UL);
    /*
     * MMU 尚未開啟，因此 early_pgd 的 runtime address 是 PA。
     */
    unsigned long pgd_pa = (unsigned long)&early_pgd[0];
    unsigned long satp_value = MAKE_SATP(pgd_pa); // 開始使用 Sv39

    asm volatile(
        "csrw satp, %0\n"
        "sfence.vma zero, zero\n"
        :
        : "r"(satp_value)
        : "memory"
    );
}

void drop_identity_map(void) {
    /*
     * 移除 VA 0 ~ 4 GiB 的 identity mappings。
     */
    for (unsigned long i = 0; i < EARLY_MAP_GIB; i++)
        early_pgd[i] = 0;

    asm volatile(
        "sfence.vma zero, zero"
        :
        :
        : "memory"
    );
}

unsigned long *vm_create_user_pgd(void) {
    /*
     * alloc_pages() 現在回傳的是
     * kernel higher-half VA。
     */
    unsigned long *pgd = (unsigned long *)alloc_pages(0);

    if (!pgd) return 0;

    clear_table(pgd);

    /*
     * Sv39:
     *
     * PGD[0..255]   → lower half / user space
     * PGD[256..511] → upper half / kernel space
     *
     * User mappings 要保持獨立，
     * 所以 low half 目前全部維持 0。
     *
     * Kernel mapping 則直接共享目前 kernel PGD entry。
     */
    for (unsigned long i = 256; i < SV39_PT_ENTRIES; i++) {
        pgd[i] = early_pgd[i];
    }

    return pgd;
}

static unsigned long *walk_create(unsigned long *pgd, unsigned long va)
{
    unsigned long vpn2 = (va >> 30) & 0x1ffUL;
    unsigned long vpn1 = (va >> 21) & 0x1ffUL;
    unsigned long vpn0 = (va >> 12) & 0x1ffUL;

    unsigned long *pmd;
    unsigned long *pte;

    /*
     * PGD -> PMD
     */
    if (!(pgd[vpn2] & PTE_V)) { // 這個 user VA 所需的 PMD 還不存在

        pmd = (unsigned long *)alloc_pages(0);

        if (!pmd) return 0;

        clear_table(pmd);

        unsigned long pmd_pa = virt_to_phys_addr((unsigned long)pmd);

        pgd[vpn2] = PA_TO_PTE(pmd_pa) | PTE_V;
    } else {
        /*
         * 這裡預期是 non-leaf entry。
         */
        if (pgd[vpn2] &
            (PTE_R | PTE_W | PTE_X))
            return 0;

        unsigned long pmd_pa =
            PTE_TO_PA(pgd[vpn2]);

        pmd = (unsigned long *)phys_to_virt_addr(pmd_pa);
    }

    /*
     * PMD -> PTE table
     */
    if (!(pmd[vpn1] & PTE_V)) {

        pte = (unsigned long *)alloc_pages(0);

        if (!pte) return 0;

        clear_table(pte);

        unsigned long pte_pa = virt_to_phys_addr((unsigned long)pte);

        pmd[vpn1] = PA_TO_PTE(pte_pa) | PTE_V;
    } else {
        if (pmd[vpn1] & (PTE_R | PTE_W | PTE_X))
            return 0;

        unsigned long pte_pa = PTE_TO_PA(pmd[vpn1]);

        pte = (unsigned long *)phys_to_virt_addr(pte_pa);
    }
    return &pte[vpn0];
}

int map_pages_to(unsigned long *pgd, unsigned long va, unsigned long size, unsigned long pa, unsigned long prot){
    if (!pgd || size == 0)
        return -1;

    /*
     * 使用 4KB pages。
     */
    unsigned long pages = (size + PAGE_SIZE - 1) /PAGE_SIZE;

    for (unsigned long i = 0; i < pages; i++) {
        unsigned long cur_va = va + i * PAGE_SIZE;
        unsigned long cur_pa = pa + i * PAGE_SIZE;
        unsigned long *entry = walk_create(pgd, cur_va);

        if (!entry)
            return -1;

        /*
         * 不希望意外覆蓋既有 mapping。
         */
        if (*entry & PTE_V)
            return -1;

        *entry = PA_TO_PTE(cur_pa) | prot | PTE_V;
    }

    return 0;
}

void vm_user_mapping_test(void)
{
    uart_puts(
        "[VM] User mapping test start\n"
    );

    unsigned long *pgd =
        vm_create_user_pgd();

    if (!pgd) {
        uart_puts(
            "[VM] PGD allocation failed\n"
        );
        return;
    }

    void *page =
        alloc_pages(0);

    if (!page) {
        uart_puts(
            "[VM] User page allocation failed\n"
        );
        return;
    }

    unsigned long page_pa =
        virt_to_phys_addr(
            (unsigned long)page
        );

    uart_puts("[VM] User PGD VA: ");
    uart_hex((unsigned long)pgd);
    uart_puts("\n");

    uart_puts("[VM] Page VA: ");
    uart_hex((unsigned long)page);
    uart_puts("\n");

    uart_puts("[VM] Page PA: ");
    uart_hex(page_pa);
    uart_puts("\n");

    if (map_pages_to(
            pgd,
            0x0,
            PAGE_SIZE,
            page_pa,
            PTE_U |
            PTE_R |
            PTE_W |
            PTE_A |
            PTE_D) == 0) {

        uart_puts(
            "[VM] map_pages_to success\n"
        );
    } else {
        uart_puts(
            "[VM] map_pages_to failed\n"
        );
    }
}