extern char uart_getc(void);
extern void uart_putc(char c);
extern void uart_puts(const char* s);
extern void uart_hex(unsigned long h);

/* Memory map */
#define PAGE_OFFSET   0xffffffc000000000UL
#define PAGE_SIZE     (1UL << 12) 
#define PMD_SIZE      (1UL << 21)
#define PGD_SIZE      (1UL << 30)

/* VA bit-field shifts (Sv39) */
#define PGD_SHIFT     30 
#define PMD_SHIFT     21
#define PTE_SHIFT     12

#define ENTRIES_PER_TABLE  512

#define KERNEL_PGD_INDEX   ((PAGE_OFFSET >> PGD_SHIFT) & 0x1FF)

#define LINEAR_MAP_GIB     4 

/* PTE descriptor bits (Sv39) */
#define PTE_V  (1UL << 0) // Valid
#define PTE_R  (1UL << 1) // Read
#define PTE_W  (1UL << 2) // Write
#define PTE_X  (1UL << 3) // Execute
#define PTE_U  (1UL << 4) // User
#define PTE_G  (1UL << 5) // Global
#define PTE_A  (1UL << 6) // Accessed
#define PTE_D  (1UL << 7) // Dirty

#define PROT_KERNEL  (PTE_V | PTE_R | PTE_W | PTE_X | PTE_G | PTE_A | PTE_D)

#define SATP_SV39           (8UL << 60) // 最高 4 bits 是 MODE, MODE = 8 代表 Sv39
#define MAKE_SATP(pgd_pa)   (SATP_SV39 | ((unsigned long)(pgd_pa) >> 12)) // satp 還要包含 root page table 的 PPN

/*
 * 建立 PTE, PTE 不是直接存完整 64 bit physical address。
 * PTE 的 PPN 只存放 physical address 的 bit 12 ~ bit 53 (pa >> 12 :移除 PA 的 page offset)。
 * 接著把 PPN 放到 PTE 的 bit 10 以上
 * 最後加入 flags, | 把權限 bits 合併進 PTE
 */
#define MAKE_PTE(pa, flags) ((((unsigned long)(pa)) >> 12) << 10 | (flags))

#define PHYS_RAM_BASE 0x80000000UL
#define UART_PHYS_BASE 0x10000000UL

#define PROT_MMIO \
    (PTE_V | PTE_R | PTE_W | PTE_G | PTE_A | PTE_D)

static unsigned long __attribute__((section(".data"), aligned(PAGE_SIZE)))
    pgd[ENTRIES_PER_TABLE] = { 0 };

static unsigned long __attribute__((section(".data"), aligned(PAGE_SIZE)))
    pmd[LINEAR_MAP_GIB][ENTRIES_PER_TABLE] = { { 0 } };

void setup_vm(void)
{
    /*
     * 每個 PMD table 有 512 個 entry。
     * 每個 entry 映射 2 MiB page。
     * 因此一個 PMD table 映射 1 GiB。
     * 共建立了4 張 PMD table
     */
    for (unsigned long i = 0; i < LINEAR_MAP_GIB; i++) {
        unsigned long region_pa = PHYS_RAM_BASE + i * PGD_SIZE;

        /*
         * 建立這 1 GiB 區域中的 512 個 2 MiB page(leaf entries)。
         */
        for (unsigned long j = 0; j < ENTRIES_PER_TABLE; j++) {
            unsigned long pa = region_pa + j * PMD_SIZE;

            pmd[i][j] = MAKE_PTE(pa, PROT_KERNEL);
        }

        /*
         * Identity mapping:
         *
         * 右移 30 bits，等同計算位於第幾個 1 GiB PGD 區域
         * & 0x1ff 是只保留最低 9 bits, 0 ~ 511, 對應到 PGD table 的 index
         * 
         * e.g. VA 0x80000000 -> PA 0x80000000
         * 0x80000000 >> 30 = 2
         * pgd[2] = MAKE_PTE((unsigned long)pmd[0], PTE_V);
         * 只有 valid bit 代表 non-leaf, 因為 pgd[2] 指向 pmd[0], pmd[0] 才是 leaf, 代表 2 MiB 的 mapping, 2 Mib RAM block
         */
        unsigned long identity_index = (region_pa >> PGD_SHIFT) & 0x1ff;

        pgd[identity_index] = MAKE_PTE((unsigned long)pmd[i], PTE_V);

        /*
         * Higher-half mapping:
         *
         * VA PAGE_OFFSET + PA -> PA
         * e.g. region pa: 0x80000000, kernel_va = 0xffffffc080000000
         * 
         * pgd[2]   → pmd[0]   // identity
         * pgd[258] → pmd[0]   // higher half
         * 
         * 兩者指向同一張 pmd[0], 這就是同一個 PA 同時擁有兩個 VA (identity mapping 與 higher-half mapping)
         */
        unsigned long kernel_va = PAGE_OFFSET + region_pa;
        unsigned long kernel_index = (kernel_va >> PGD_SHIFT) & 0x1ff;

        pgd[kernel_index] = MAKE_PTE((unsigned long)pmd[i], PTE_V);
    }

    /*
     * UART 位於 PA 0x10000000。
     *
     * 這裡使用一個 1 GiB PGD leaf，將：
     *
     * VA 0xffffffc000000000 ～ 0xffffffc03fffffff
     * 映射到
     * PA 0x00000000 ～ 0x3fffffff
     *
     * 因此 UART 的 higher-half VA 為：
     * 0xffffffc010000000
     */
    pgd[KERNEL_PGD_INDEX] = MAKE_PTE(0x0, PROT_MMIO);

    /*
     * satp.PPN 必須放 root page table 的 physical page number。
     *
     * 此時 MMU 尚未開啟，pgd 的 runtime address 是 physical address。
     */
    unsigned long satp = MAKE_SATP((unsigned long)pgd);

    /*
     * csrw satp, register 從這時起，MMU 開始使用 pgd 做位址轉換
     * sfence.vma 是 RISC-V 的指令, 用來清除舊的虛擬位址轉換快取，也就是 TLB
     */
    asm volatile(
        "csrw satp, %0\n"
        "sfence.vma zero, zero\n"
        :
        : "r"(satp)
        : "memory"
    );
}

void drop_identity_map(void)
{
    /*
     * 清除 RAM 的 identity mappings。
     *
     * 4 GiB RAM 對應的 PGD indexes：
     *   0x80000000  -> index 2
     *   0xc0000000  -> index 3
     *   0x100000000 -> index 4
     *   0x140000000 -> index 5
     */
    for (unsigned long i = 0; i < LINEAR_MAP_GIB; i++) {
        unsigned long pa = PHYS_RAM_BASE + i * PGD_SIZE;
        unsigned long identity_index = (pa >> PGD_SHIFT) & 0x1ff;

        pgd[identity_index] = 0;
    }

    /*
     * 清除 TLB 中可能殘留的 identity translations。
     */
    asm volatile(
        "sfence.vma zero, zero"
        :
        :
        : "memory"
    );
}

void start_kernel(void)
{
    uart_puts("\nStarting kernel at : ");
    uart_hex((unsigned long)start_kernel);
    uart_puts("\n");
    while (1) {
        uart_putc(uart_getc());
    }
}