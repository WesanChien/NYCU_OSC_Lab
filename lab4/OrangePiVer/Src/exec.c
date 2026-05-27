#include "exec.h"
#include "initrd.h"
#include "uart.h"

#define SSTATUS_SPP  (1UL << 8)
#define SSTATUS_SPIE (1UL << 5)

/*
 * 暫時給 user program 使用的 stack。
 *
 * 這裡不是完整 process stack，只是為了讓 U-mode 程式有一塊堆疊可用。
 * stack 往低位址成長，所以初始 sp 會設在 user_stack 陣列尾端。
 */
static unsigned char user_stack[4096] __attribute__((aligned(16)));

int exec_user_program(const char *filename) {
    const char *entry = 0;
    unsigned long size = 0;

    if (initrd_find_file(filename, &entry, &size) < 0) { // 從 initrd 找到 user program 的 entry point 和 size
        uart_puts("[exec] file not found: ");
        uart_puts(filename);
        uart_puts("\n");
        return -1;
    }

    uart_puts("[exec] entry: ");
    uart_hex((unsigned long)entry);
    uart_puts(", size: ");
    uart_hex(size);
    uart_puts("\n");

    if ((unsigned long)entry == 0) {
        uart_puts("[exec] invalid entry address\n");
        return -1;
    }

    unsigned long user_sp = (unsigned long)user_stack + sizeof(user_stack); // user stack 往低位址成長，所以初始 sp 設在陣列尾端

    asm volatile(
        /*
         * Save current kernel stack into sscratch.
         * Trap entry will use sscratch to switch back to kernel stack.
         */
        "mv t0, sp\n"
        "csrw sscratch, t0\n"

        /*
         * sret will jump to sepc.
         */
        "csrw sepc, %[entry]\n"

        /*
         * Switch current sp to user stack before entering U-mode.
         */
        "mv sp, %[user_sp]\n"

        /*
         * Clear sstatus.SPP.
         * SPP = 0 means sret returns to U-mode.
         */
        "li t0, %[spp]\n"
        "csrc sstatus, t0\n"

        /*
         * Set sstatus.SPIE.
         */
        "li t0, %[spie]\n"
        "csrs sstatus, t0\n"

        "sret\n"
        :
        : [entry] "r"(entry),
          [user_sp] "r"(user_sp),
          [spp] "i"(1UL << 8),
          [spie] "i"(1UL << 5)
        : "t0", "memory"
    );

    return 0;
}