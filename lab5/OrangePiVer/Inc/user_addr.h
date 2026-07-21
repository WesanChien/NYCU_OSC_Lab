#ifndef USER_ADDR_H
#define USER_ADDR_H

/*
 * User program 載入位置
 *
 * 這個位址要滿足：
 * 1. 不和 kernel image 重疊
 * 2. 不和 initrd / DTB / allocator 管理區衝突
 * 3. 若 user binary 是用固定位址 link，這裡要和 user linker script 一致
 * 
 * 當你執行 exec XXXX
 * kernel 會把 XXXX 的內容 copy 到 0x03000000
 */
#define USER_PROG_BASE      0x03000000UL
#define USER_PROG_MAX_SIZE  0x00400000UL   /* 4 MB */

/*
 * user program 最後一 page 保留給 signal trampoline
 */
#define USER_SIGTRAMP_SIZE      0x1000UL
#define USER_SIGTRAMP_BASE      (USER_PROG_BASE + USER_PROG_MAX_SIZE - USER_SIGTRAMP_SIZE)

/*
 * 真正 user binary 可使用的最大大小
 */
#define USER_PROG_LOAD_MAX_SIZE (USER_PROG_MAX_SIZE - USER_SIGTRAMP_SIZE)

#endif