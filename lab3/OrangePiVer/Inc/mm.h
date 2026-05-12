#ifndef MM_H
#define MM_H

#define PAGE_SIZE       4096UL
#define PAGE_SHIFT      12

#define MM_BASE         0x10000000UL
#define MM_SIZE         0x10000000UL

#define MAX_ORDER       12
#define MAX_ALLOC_SIZE  (PAGE_SIZE * (1UL << MAX_ORDER))

/*
 * 用 static frame_array，
 * 因此需設定最多支援多少 physical memory
 *
 * OrangePi RV2 第一段 memory region 範例是 2 GB，
 * 所以這裡先支援最多 2 GB
 *
 * Advanced Exercise 3 會改成 startup allocator 動態配置 frame_array
 */
#define MAX_MANAGED_MEMORY_SIZE 0x80000000UL
#define MAX_FRAME_COUNT         (MAX_MANAGED_MEMORY_SIZE / PAGE_SIZE)

void mm_init(const void *fdt, unsigned long initrd_start, unsigned long initrd_end);

void *alloc_pages(unsigned int order);
void free_pages(void *addr);

void *allocate(unsigned long size);
void free(void *ptr);

void memory_reserve(unsigned long start, unsigned long size);

void mm_dump(void);
void mm_test(void);

#endif