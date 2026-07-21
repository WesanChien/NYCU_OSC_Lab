#include "user_loader.h"
#include "user_addr.h"
#include "initrd.h"
#include "uart.h"
#include "signal.h"

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

int user_load_image_from_initrd(const char *path, void **entry) {
    const char *file_data = 0;
    unsigned long file_size = 0;

    if (path == 0 || entry == 0)
        return -1;

    if (initrd_find_file(path, &file_data, &file_size) < 0) {
        uart_puts("[exec] file not found: ");
        uart_puts(path);
        uart_puts("\n");
        return -1;
    }

    if (file_size == 0 || file_size > USER_PROG_MAX_SIZE) {
        uart_puts("[exec] invalid file size\n");
        return -1;
    }

    mem_zero((void *)USER_PROG_BASE, USER_PROG_MAX_SIZE); // 清空舊的 user program 載入區
    mem_copy((void *)USER_PROG_BASE, file_data, file_size); // 把 initrd 裡的 binary copy 到固定 user program base(執行位置 0x03000000)

    /*
     * exec 會清掉整個 USER_PROG_BASE 區域，
     * 所以每次載入 user program 後都要重新放 signal trampoline。
     */
    signal_install_trampoline();

    asm volatile("fence.i" ::: "memory"); // 如果沒有 fence.i，instruction side 可能還不知道這段 memory: 0x03000000 已經被改成新的 code
    *entry = (void *)USER_PROG_BASE; // entry 必須是 USER_PROG_BASE，不是 file_data

    return 0;
}