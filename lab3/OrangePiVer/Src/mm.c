#include "mm.h"
#include "uart.h"
#include "fdt.h"

#define NULL ((void *)0)

#define NUM_POOLS       8

#define FRAME_FREE      0
#define FRAME_USED      1

static unsigned long managed_base = 0;
static unsigned long managed_size = 0;
static unsigned long frame_count = 0;

extern char __kernel_start[];
extern char __kernel_end[];

struct page { // 每個 page 的 metadata
    int order;
    int state;
    int in_free_list;

    /*
     * chunk_size != 0 表示這個 page 被 dynamic allocator 拿去切 chunk。
     * chunk_size == 0 表示這個 page 是一般 page allocation。
     */
    unsigned long chunk_size;
    int chunk_pool;

    struct page *prev;
    struct page *next;
};

static struct page frame_array[MAX_FRAME_COUNT];
/*
 * free_area[order] 是 linked list head。
 *
 * free_area[0]  管理 1 page block
 * free_area[1]  管理 2 pages block
 * free_area[2]  管理 4 pages block
 * ...
 * free_area[12] 管理 4096 pages block
 */
static struct page free_area[MAX_ORDER + 1];

/*
 * Dynamic allocator 的 chunk pools
 * 16, 32, 64, 128, 256, 512, 1024, 2048 bytes
 * 
 * chunk_free_list[0] -> 16-byte chunks, ..., chunk_free_list[7] -> 2048-byte chunks
 * 
 * if allocate(2049) go buddy system instead of chunk allocator
 */
static unsigned long pool_sizes[NUM_POOLS] = {
    16, 32, 64, 128, 256, 512, 1024, 2048
};

static void *chunk_free_list[NUM_POOLS];

static int mm_initialized = 0;

/* ---------- small helpers ---------- */

static void print_dec(unsigned long x) {
    char buf[32];
    int i = 0;

    if (x == 0) {
        uart_putc('0');
        return;
    }

    while (x > 0) {
        buf[i++] = '0' + (x % 10);
        x /= 10;
    }

    while (i > 0)
        uart_putc(buf[--i]);
}

static unsigned long align_up(unsigned long val, unsigned long align) {
    return (val + align - 1) & ~(align - 1);
}

static unsigned int size_to_order(unsigned long size) {
    unsigned long pages = (size + PAGE_SIZE - 1) >> PAGE_SHIFT;
    unsigned int order = 0;
    unsigned long n = 1;

    while (n < pages) {
        n <<= 1;
        order++;
    }

    return order;
}

static unsigned long frame_index(struct page *page) {
    return (unsigned long)(page - frame_array);
}

static unsigned long frame_addr(unsigned long idx) {
    return managed_base + idx * PAGE_SIZE;
}

static unsigned long addr_to_frame_index(unsigned long addr) {
    return (addr - managed_base) >> PAGE_SHIFT;
}

static int addr_in_managed_range(unsigned long addr) {
    return addr >= managed_base && addr < (managed_base + managed_size);
}

static unsigned long align_down(unsigned long val, unsigned long align) {
    return val & ~(align - 1);
}

/* ---------- circular doubly linked list ---------- */

static void list_init(struct page *head) {
    head->next = head;
    head->prev = head;
}

static int list_empty(struct page *head) {
    return head->next == head;
}

static void list_add(struct page *head, struct page *node) {
    node->next = head->next;
    node->prev = head;

    head->next->prev = node;
    head->next = node;
}

static void list_del(struct page *node) {
    node->prev->next = node->next;
    node->next->prev = node->prev;

    node->next = NULL;
    node->prev = NULL;
}

/* ---------- log helpers ---------- */

static void log_add_block(unsigned long idx, unsigned int order) {
    uart_puts("[+] Add page ");
    print_dec(idx);
    uart_puts(" to order ");
    print_dec(order);
    uart_puts(". Range of pages: [");
    print_dec(idx);
    uart_puts(", ");
    print_dec(idx + (1UL << order) - 1);
    uart_puts("]\n");
}

static void log_remove_block(unsigned long idx, unsigned int order) {
    uart_puts("[-] Remove page ");
    print_dec(idx);
    uart_puts(" from order ");
    print_dec(order);
    uart_puts(". Range of pages: [");
    print_dec(idx);
    uart_puts(", ");
    print_dec(idx + (1UL << order) - 1);
    uart_puts("]\n");
}

static void log_buddy(unsigned long idx, unsigned long buddy_idx, unsigned int order) {
    uart_puts("[*] Buddy found! buddy idx: ");
    print_dec(buddy_idx);
    uart_puts(" for page ");
    print_dec(idx);
    uart_puts(" with order ");
    print_dec(order);
    uart_puts("\n");
}

/* ---------- free-area operations ---------- */

static void add_free_block(unsigned long idx, unsigned int order) {
    struct page *page = &frame_array[idx];

    page->order = order;
    page->state = FRAME_FREE;
    page->in_free_list = 1;
    page->chunk_size = 0;
    page->chunk_pool = -1;

    list_add(&free_area[order], page);

    log_add_block(idx, order);
}

static void remove_free_block(struct page *page) {
    unsigned long idx = frame_index(page);
    unsigned int order = (unsigned int)page->order;

    list_del(page);

    page->in_free_list = 0;

    log_remove_block(idx, order);
}

/* ---------- Basic Exercise 1: Buddy System ---------- */

void *alloc_pages(unsigned int order) {
    if (order > MAX_ORDER)
        return NULL;

    for (unsigned int current_order = order; current_order <= MAX_ORDER; current_order++) {
        if (!list_empty(&free_area[current_order])) {
            struct page *page = free_area[current_order].next; // page 是 struct page*，指向 block 起始 page 的 metadata, 它不是實際 memory 裡面的資料指標

            remove_free_block(page);

            while (current_order > order) { // 如果找到的 block 比 request 的 block 大，就要不斷 split
                current_order--;

                unsigned long idx = frame_index(page); // pfn
                unsigned long buddy_idx = idx + (1UL << current_order); // buddy pfn

                /*
                 * 左半邊繼續拿來 split 或 allocate。
                 * 右半邊是 redundant block，放回 free list。
                 */
                add_free_block(buddy_idx, current_order);

                page->order = current_order;
            }

            page->order = order;
            page->state = FRAME_USED;
            page->in_free_list = 0;
            page->chunk_size = 0;
            page->chunk_pool = -1;

            uart_puts("[Page] Allocate ");
            uart_hex(frame_addr(frame_index(page)));
            uart_puts(" at order ");
            print_dec(order);
            uart_puts(", page ");
            print_dec(frame_index(page));
            uart_puts("\n");

            return (void *)frame_addr(frame_index(page));
        }
    }

    uart_puts("[Page] Allocation failed: out of memory\n");
    return NULL;
}

void free_pages(void *addr) {
    if (!addr)
        return;

    unsigned long a = (unsigned long)addr;

    if (!addr_in_managed_range(a)) {
        uart_puts("[Page] Free failed: address out of managed range\n");
        return;
    }

    /*
     * page allocator 分配出去的 address 應該要 page-aligned。
     *
     * 例如：
     * 0x10000000 可以
     * 0x10001000 可以
     * 0x10000010 不可以
     */
    if ((a & (PAGE_SIZE - 1)) != 0) {
        uart_puts("[Page] Free failed: address is not page-aligned\n");
        return;
    }

    unsigned long idx = addr_to_frame_index(a);
    struct page *page = &frame_array[idx];
    unsigned int order = (unsigned int)page->order;

    page->state = FRAME_FREE;
    page->chunk_size = 0;
    page->chunk_pool = -1;

    
    while (order < MAX_ORDER) { // 開始嘗試跟 buddy merge
        /*
         * 找 buddy 的公式：buddy_idx = idx XOR 2^order
         *
         * 例如 order = 1，block size = 2 pages：
         *
         * idx = 0 -> buddy_idx = 0 ^ 2 = 2
         * idx = 2 -> buddy_idx = 2 ^ 2 = 0
         * 
         * [0 1] 的 buddy 是 [2 3]
         * [2 3] 的 buddy 是 [0 1]
         */
        unsigned long buddy_idx = idx ^ (1UL << order);

        if (buddy_idx >= frame_count)
            break;

        struct page *buddy = &frame_array[buddy_idx];

        /*
         * 可以 merge 的條件：
         *
         * 1. buddy 必須在 free list 裡
         * 2. buddy 必須是 free
         * 3. buddy 的 order 必須跟自己一樣
         *
         * 如果 buddy 已經被分配出去，不能 merge。
         * 如果 buddy 的 order 不一樣，也不能 merge。
         */
        if (!buddy->in_free_list ||
            buddy->state != FRAME_FREE ||
            buddy->order != (int)order) {
            break;
        }

        log_buddy(idx, buddy_idx, order);

        remove_free_block(buddy);

        if (buddy_idx < idx)
            idx = buddy_idx;

        page = &frame_array[idx];
        order++;
        page->order = order;
        page->state = FRAME_FREE;
    }

    add_free_block(idx, order);

    uart_puts("[Page] Free ");
    uart_hex(a);
    uart_puts(" and add back to order ");
    print_dec(order);
    uart_puts(", page ");
    print_dec(idx);
    uart_puts("\n");
}

/* ---------- Dynamic Memory Allocator (Chunk Allocator) ---------- */

static int find_pool(unsigned long size) { // 用途是找到最小, 但足夠大的 pool
    for (int i = 0; i < NUM_POOLS; i++) {
        if (size <= pool_sizes[i]) return i;
    }

    return -1;
}

/*
 * 每個 free chunk 的前幾個 bytes 被拿來存「下一個 free chunk 的 pointer」
 * 等於把 chunk 自己當作 linked list node 使用
 */
static void push_chunk(int pool, void *ptr) {
    *(void **)ptr = chunk_free_list[pool];
    chunk_free_list[pool] = ptr;
}

static void *pop_chunk(int pool) {
    void *ptr = chunk_free_list[pool];

    if (ptr) chunk_free_list[pool] = *(void **)ptr;

    return ptr;
}

static void refill_chunk_pool(int pool) { // 當某 chunk pool 沒有 free chunk 時，要向 Buddy System 要一個 page，然後切成 chunks
    unsigned long chunk_size = pool_sizes[pool];

    void *page_addr = alloc_pages(0); // 向 Buddy System 要 2^0 = 1 個 page

    if (!page_addr)
        return;

    unsigned long page_idx = addr_to_frame_index((unsigned long)page_addr); // 把 page address 轉成 page index, 才能找到對應的 frame_array metadata

    frame_array[page_idx].chunk_size = chunk_size; // 記錄這 page 是被 chunk allocator 使用
    frame_array[page_idx].chunk_pool = pool;

    /*
     * 把整個 page 切成很多個 chunk
     *
     * 例如 chunk_size = 16：
     *
     * page 4096 bytes 可以切成：
     * 4096 / 16 = 256 個 chunks
     *
     * off = 0, 16, 32, 48, ...
     */
    for (unsigned long off = 0; off + chunk_size <= PAGE_SIZE; off += chunk_size) { 
        push_chunk(pool, (void *)((unsigned long)page_addr + off));
    }

    uart_puts("[Chunk] Refill pool size ");
    print_dec(chunk_size);
    uart_puts(" from page ");
    print_dec(page_idx);
    uart_puts("\n");
}

static void *alloc_chunk(unsigned long size) {
    int pool = find_pool(size);
    if (pool < 0) return NULL; // 沒有合適 size 的 chunk pool，就走 buddy system

    if (!chunk_free_list[pool]) // 如果這 pool 沒有 free chunk，就向 Buddy System 要一個 page 補貨
        refill_chunk_pool(pool);

    void *ptr = pop_chunk(pool); // 從 pool 裡拿出一個 chunk

    if (!ptr) {
        uart_puts("[Chunk] Allocation failed\n");
        return NULL;
    }

    uart_puts("[Chunk] Allocate ");
    uart_hex((unsigned long)ptr);
    uart_puts(" at chunk size ");
    print_dec(pool_sizes[pool]);
    uart_puts("\n");

    return ptr;
}

static void free_chunk(void *ptr, int pool) {
    push_chunk(pool, ptr);

    uart_puts("[Chunk] Free ");
    uart_hex((unsigned long)ptr);
    uart_puts(" at chunk size ");
    print_dec(pool_sizes[pool]);
    uart_puts("\n");
}

void *allocate(unsigned long size) {
    if (size == 0)
        return NULL;

    if (size > MAX_ALLOC_SIZE) {
        uart_puts("[Alloc] Allocation failed: size too large\n");
        return NULL;
    }

    /*
     * 小於等於 2048 bytes 的 request 交給 chunk allocator
     * 更大的 request 直接用 page allocator。
     */
    if (size <= 2048)
        return alloc_chunk(size);

    unsigned int order = size_to_order(size);

    return alloc_pages(order);
}

void free(void *ptr) {
    if (!ptr)
        return;

    unsigned long addr = (unsigned long)ptr;

    if (!addr_in_managed_range(addr)) {
        uart_puts("[Free] Ignore pointer outside managed range\n");
        return;
    }

    unsigned long page_idx = addr_to_frame_index(addr);
    struct page *page = &frame_array[page_idx];

    if (page->chunk_size != 0) {
        free_chunk(ptr, page->chunk_pool);
        return;
    }

    free_pages(ptr);
}

/* ---------- init / debug / test ---------- */

void mm_dump(void) {
    uart_puts("Memory allocator state:\n");
    uart_puts("  managed range: ");
    uart_hex(managed_base);
    uart_puts(" - ");
    uart_hex(managed_base + managed_size);
    uart_puts("\n");

    uart_puts("  managed size: ");
    uart_hex(managed_size);
    uart_puts("\n");

    uart_puts("  frame count: ");
    print_dec(frame_count);
    uart_puts("\n");

    for (int order = MAX_ORDER; order >= 0; order--) {
        unsigned long count = 0;
        struct page *head = &free_area[order];

        for (struct page *p = head->next; p != head; p = p->next)
            count++;

        uart_puts("  free_area[");
        print_dec(order);
        uart_puts("] ");
        print_dec(count);
        uart_puts("\n");
    }
}

// void mm_init(void) { // 把 hardcoded memory range 初始化成 Buddy System 可管理的狀態
//     if (mm_initialized)
//         return;

//     uart_puts("[MM] Initializing memory allocator\n");

//     for (int i = 0; i <= MAX_ORDER; i++) // 初始化所有 free_area linked list
//         list_init(&free_area[i]);

//     for (unsigned long i = 0; i < frame_count; i++) { // 初始化所有 frame_array metadata
//         frame_array[i].order = -1;
//         frame_array[i].state = FRAME_USED;
//         frame_array[i].in_free_list = 0;
//         frame_array[i].chunk_size = 0;
//         frame_array[i].chunk_pool = -1;
//         frame_array[i].next = NULL;
//         frame_array[i].prev = NULL;
//     }

//     for (int i = 0; i < NUM_POOLS; i++) // 初始化 chunk allocator 的 free lists
//         chunk_free_list[i] = NULL;

//     /*
//      * 把整段 hardcoded memory region 切成 Buddy System blocks。
//      * 這裡用通用寫法，未來如果 MM_SIZE 不剛好對齊也能處理。
//      */
//     unsigned long idx = 0;

//     while (idx < frame_count) {
//         int selected_order = 0;

//         for (int order = MAX_ORDER; order >= 0; order--) {
//             unsigned long block_pages = 1UL << order;

//             if ((idx % block_pages) == 0 &&
//                 idx + block_pages <= frame_count) {
//                 selected_order = order;
//                 break;
//             }
//         }

//         add_free_block(idx, selected_order);
//         idx += 1UL << selected_order;
//     }

//     mm_initialized = 1;

//     uart_puts("[MM] Memory allocator initialized\n");
// }

void mm_init(const void *fdt, unsigned long initrd_start, unsigned long initrd_end) {
    if (mm_initialized)
        return;

    uart_puts("[MM] Initializing memory allocator\n");

    unsigned long dtb_mem_base = 0;
    unsigned long dtb_mem_size = 0;

    if (!dtb_get_memory_region(fdt, &dtb_mem_base, &dtb_mem_size)) { // 讀取 DTB memory region
        uart_puts("[MM] Failed to get /memory region from DTB\n");
        while (1) { }
    }
    
    managed_base = align_up(dtb_mem_base, PAGE_SIZE); // 對齊 page boundary, buddy system 是用 page 當單位
    managed_size = dtb_mem_size - (managed_base - dtb_mem_base);
    managed_size = align_down(managed_size, PAGE_SIZE);
    /*
     * 使用 DTB /memory node 的第一段 memory region
     *
     * 目前 frame_array 還是 static array，
     * 所以最多先支援 MAX_MANAGED_MEMORY_SIZE。
     */
    if (managed_size > MAX_MANAGED_MEMORY_SIZE)
        managed_size = MAX_MANAGED_MEMORY_SIZE;

    frame_count = managed_size / PAGE_SIZE;

    uart_puts("[MM] DTB memory base: ");
    uart_hex(dtb_mem_base);
    uart_puts("\n");

    uart_puts("[MM] DTB memory size: ");
    uart_hex(dtb_mem_size);
    uart_puts("\n");

    uart_puts("[MM] Managed base: ");
    uart_hex(managed_base);
    uart_puts("\n");

    uart_puts("[MM] Managed size: ");
    uart_hex(managed_size);
    uart_puts("\n");

    uart_puts("[MM] Frame count: ");
    print_dec(frame_count);
    uart_puts("\n");

    if (frame_count == 0 || frame_count > MAX_FRAME_COUNT) {
        uart_puts("[MM] Invalid frame count\n");
        while (1) { }
    }

    for (int i = 0; i <= MAX_ORDER; i++)
        list_init(&free_area[i]);

    for (unsigned long i = 0; i < frame_count; i++) {
        frame_array[i].order = -1;
        frame_array[i].state = FRAME_USED;
        frame_array[i].in_free_list = 0;
        frame_array[i].chunk_size = 0;
        frame_array[i].chunk_pool = -1;
        frame_array[i].next = NULL;
        frame_array[i].prev = NULL;
    }

    for (int i = 0; i < NUM_POOLS; i++)
        chunk_free_list[i] = NULL;

    /*
     * 先把整段 managed memory 放進 buddy system。
     * 後面再 reserve 已經被使用的區域。
     */
    unsigned long idx = 0;

    while (idx < frame_count) {
        int selected_order = 0;

        for (int order = MAX_ORDER; order >= 0; order--) { // 從第 0 個 page 開始, 盡量用最大的 order block 填滿整段 managed memory
            unsigned long block_pages = 1UL << order;

            if ((idx % block_pages) == 0 &&
                idx + block_pages <= frame_count) {
                selected_order = order;
                break;
            }
        }

        add_free_block(idx, selected_order);
        idx += 1UL << selected_order;
    }

    /*
     * Reserve 1: DTB itself
     *
     * DTB 是 bootloader 傳給 kernel 的硬體描述資料
     *
     * fdt 是 DTB 起始位址。
     * fdt_totalsize(fdt) 是 DTB blob 總長度。
     */
    unsigned long dtb_start = (unsigned long)fdt;
    unsigned long dtb_size = fdt_totalsize(fdt);

    memory_reserve(dtb_start, dtb_size);

    /*
     * Reserve 2: Kernel image
     *
     * 這兩個 symbol 要在 linker script 裡定義。
     * 建議 reserve 到 __kernel_end，
     * 包含 .text/.rodata/.data/.bss，以及 static frame_array。
     */
    unsigned long kernel_start = (unsigned long)__kernel_start;
    unsigned long kernel_end = (unsigned long)__kernel_end;

    if (kernel_end > kernel_start)
        memory_reserve(kernel_start, kernel_end - kernel_start);

    /*
     * Reserve 3: Initramfs
     *
     * Lab2 已經從 /chosen 讀出 linux,initrd-start/end。
     */
    if (initrd_start && initrd_end && initrd_end > initrd_start)
        memory_reserve(initrd_start, initrd_end - initrd_start);

    mm_initialized = 1;

    uart_puts("[MM] Memory allocator initialized\n");
}

/*
 * 把某段 address range 對應到 page index，
 * 再從 buddy free lists 裡把這些 pages 移除。
 */
void memory_reserve(unsigned long start, unsigned long size) {
    if (size == 0)
        return;

    if (managed_size == 0 || frame_count == 0)
        return;

    unsigned long end = start + size;
    unsigned long managed_end = managed_base + managed_size;

    /*
     * 如果 reserved range 完全不在 managed memory 裡，
     * 那就不需要處理。
     */
    if (end <= managed_base || start >= managed_end)
        return;

    /*
     * 取交集。
     *
     * reserved range:
     *   [start, end)
     *
     * managed range:
     *   [managed_base, managed_end)
     */
    if (start < managed_base)
        start = managed_base;

    if (end > managed_end)
        end = managed_end;

    /*
     * 只要碰到某個 page 的一部分，就 reserve 整個 page。
     */
    unsigned long reserve_start = align_down(start, PAGE_SIZE);
    unsigned long reserve_end = align_up(end, PAGE_SIZE);

    unsigned long start_idx = (reserve_start - managed_base) >> PAGE_SHIFT;
    unsigned long end_idx = (reserve_end - managed_base) >> PAGE_SHIFT;

    if (end_idx > frame_count)
        end_idx = frame_count;

    uart_puts("[Reserve] Reserve address [");
    uart_hex(reserve_start);
    uart_puts(", ");
    uart_hex(reserve_end);
    uart_puts("). Range of pages: [");
    print_dec(start_idx);
    uart_puts(", ");
    print_dec(end_idx);
    uart_puts(")\n");

    /*
     * Top-down splitting。
     *
     * 從大 order 掃到小 order。
     *
     * No overlap:
     *   block 不動。
     *
     * Full overlap:
     *   從 free list 移除，標成 used/reserved。
     *
     * Partial overlap:
     *   從 free list 移除，切成兩個 child blocks，
     *   放到 order - 1，下一輪繼續檢查。
     */
    for (int order = MAX_ORDER; order >= 0; order--) {
        struct page *head = &free_area[order];
        struct page *p = head->next;

        while (p != head) {
            struct page *next = p->next;

            unsigned long idx = frame_index(p);
            unsigned long block_pages = 1UL << order;
            unsigned long block_end = idx + block_pages;

            int no_overlap =
                block_end <= start_idx || idx >= end_idx;

            if (no_overlap) {
                p = next;
                continue;
            }

            remove_free_block(p);

            int full_overlap =
                start_idx <= idx && block_end <= end_idx;

            if (full_overlap || order == 0) {
                /*
                 * 這個 block 被完整 reserve。
                 *
                 * 注意：
                 * 它已經被 remove_free_block() 從 free list 移除了。
                 * 不需要再放回去。
                 */
                p->order = order;
                p->state = FRAME_USED;
                p->in_free_list = 0;
                p->chunk_size = 0;
                p->chunk_pool = -1;
            } else {
                /*
                 * Partial overlap。
                 *
                 * 例：
                 *   一個 order 4 block 有 16 pages。
                 *   reserved range 只碰到其中幾個 pages。
                 *
                 * 不能整個 reserve，也不能整個 free。
                 * 所以切成兩個 order 3 blocks，下一輪繼續判斷。
                 */
                unsigned int child_order = order - 1;
                unsigned long half = 1UL << child_order;

                add_free_block(idx, child_order);
                add_free_block(idx + half, child_order);
            }

            p = next;
        }
    }
}

void mm_test(void) {
    uart_puts("Testing memory allocation...\n");

    char *ptr1 = (char *)allocate(4000);
    char *ptr2 = (char *)allocate(8000);
    char *ptr3 = (char *)allocate(4000);
    char *ptr4 = (char *)allocate(4000);

    free(ptr1);
    free(ptr2);
    free(ptr3);
    free(ptr4);

    uart_puts("Testing dynamic allocator...\n");

    char *kmem_ptr1 = (char *)allocate(16);
    char *kmem_ptr2 = (char *)allocate(32);
    char *kmem_ptr3 = (char *)allocate(64);
    char *kmem_ptr4 = (char *)allocate(128);

    free(kmem_ptr1);
    free(kmem_ptr2);
    free(kmem_ptr3);
    free(kmem_ptr4);

    char *kmem_ptr5 = (char *)allocate(16);
    char *kmem_ptr6 = (char *)allocate(32);

    free(kmem_ptr5);
    free(kmem_ptr6);

    void *kmem_ptr[102];

    for (int i = 0; i < 100; i++)
        kmem_ptr[i] = allocate(128);

    for (int i = 0; i < 100; i++)
        free(kmem_ptr[i]);

    char *kmem_ptr7 = (char *)allocate(MAX_ALLOC_SIZE + 1);

    if (kmem_ptr7 == NULL) {
        uart_puts("Allocation failed as expected for size > MAX_ALLOC_SIZE\n");
    } else {
        uart_puts("Unexpected allocation success for size > MAX_ALLOC_SIZE\n");
        free(kmem_ptr7);
    }

    uart_puts("Memory allocation test finished\n");
}