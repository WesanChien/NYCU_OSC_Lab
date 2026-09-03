#ifndef VM_H
#define VM_H

/*
 * Kernel linear mapping:
 *
 *   kernel VA = physical address + KERNEL_VA_OFFSET
 */
#define KERNEL_VA_OFFSET        0xffffffc000000000UL

/*
 * Sv39 configuration
 */
#define SV39_PAGE_SHIFT         12UL
#define SV39_PMD_SHIFT          21UL
#define SV39_PGD_SHIFT          30UL

#define SV39_PAGE_SIZE          (1UL << SV39_PAGE_SHIFT)
#define SV39_PMD_SIZE           (1UL << SV39_PMD_SHIFT)
#define SV39_PGD_SIZE           (1UL << SV39_PGD_SHIFT)

#define SV39_PT_ENTRIES         512UL

/*
 * satp.MODE = 8 表示 Sv39。
 */
#define SATP_SV39               (8UL << 60)

/*
 * RISC-V PTE flags
 */
#define PTE_V                   (1UL << 0)
#define PTE_R                   (1UL << 1)
#define PTE_W                   (1UL << 2)
#define PTE_X                   (1UL << 3)
#define PTE_U                   (1UL << 4)
#define PTE_G                   (1UL << 5)
#define PTE_A                   (1UL << 6)
#define PTE_D                   (1UL << 7)

/*
 * Kernel normal memory：
 * V-R-W-X-G-A-D
 */
#define KERNEL_PAGE_FLAGS (PTE_V | PTE_R | PTE_W | PTE_X | PTE_G | PTE_A | PTE_D)
#define MMIO_PAGE_FLAGS (PTE_V | PTE_R | PTE_W | PTE_G | PTE_A | PTE_D)

/*
 * User page flags
 */
#define USER_PAGE_BASE_FLAGS (PTE_V | PTE_U | PTE_A | PTE_D)

#define USER_CODE_FLAGS (USER_PAGE_BASE_FLAGS | PTE_R | PTE_X)

#define USER_STACK_FLAGS (USER_PAGE_BASE_FLAGS | PTE_R | PTE_W)

/*
 * PTE bits [53:10] 儲存 physical page number。
 */
#define PA_TO_PTE(pa) ((((unsigned long)(pa)) >> SV39_PAGE_SHIFT) << 10)

#define PTE_TO_PA(pte) ((((unsigned long)(pte)) >> 10) << SV39_PAGE_SHIFT)

#define MAKE_SATP(pgd_pa) (SATP_SV39 | ((unsigned long)(pgd_pa) >> SV39_PAGE_SHIFT))

static inline unsigned long phys_to_virt_addr(unsigned long pa) {
    return pa + KERNEL_VA_OFFSET;
}

static inline unsigned long virt_to_phys_addr(unsigned long va) {
    return va - KERNEL_VA_OFFSET;
}

void setup_vm(void);
void drop_identity_map(void);

unsigned long *vm_create_user_pgd(void);

int map_pages_to(
    unsigned long *pgd,
    unsigned long va,
    unsigned long size,
    unsigned long pa,
    unsigned long prot
);

void vm_user_mapping_test(void);

#endif