#include <iostream>
#include <vector>
#include <list>

#define PAGE_SIZE (1UL << 12) // page size 4KB
#define NUM_PAGES 0x280000 // 2621440 pages
#define MAX_ORDER 10
typedef unsigned long phys_addr_t;

struct page {
    int order = 0;
    int refcount = 0;
};

std::vector<struct page> mem_map;
std::vector<std::list<struct page*>> free_area;

/* 保留一段 physical memory range，避免 Buddy System 之後分配到它。
 * base：reserved region 的起始 physical address
 * size：reserved region 的 byte 數
 * 目標：把 [base, base + size) 覆蓋到的 pages 從 free_area 中移除，並標記為 refcount = 1
 */ 
void memory_reserve(phys_addr_t base, size_t size) {
    if (size == 0)
        return;

    size_t start_pfn = base / PAGE_SIZE; // 起始 physical address 轉成起始 PFN, e.g. base = 0x0000 -> start_pfn = 0, base = 0x1000 -> start_pfn = 1
    size_t end_pfn = (base + size + PAGE_SIZE - 1) / PAGE_SIZE; // 只要 reserved region 碰到某個 page 的一部分，那個 page 就不能再被分配出去, [start_pfn, end_pfn) 是左閉右開區間

    if (start_pfn >= NUM_PAGES)
        return;
    if (end_pfn > NUM_PAGES)
        end_pfn = NUM_PAGES;

    if (start_pfn >= end_pfn)
        return;

    for (int order = MAX_ORDER; order >= 0; --order) { // 掃描所有的 order
        for (auto it = free_area[order].begin(); it != free_area[order].end(); ) { // 掃描目前 order 的所有 free blocks
            struct page* page = *it; // 取得目前正在檢查的 free block 起始 page
            size_t pfn = page - mem_map.data(); // 把 page pointer 轉成 PFN。
            size_t block_pages = 1UL << order; // block size
            size_t block_end = pfn + block_pages;

            /* reserved range 是：
             * [start_pfn, end_pfn)
             *
             * block range 是：
             * [pfn, block_end)
             */ 

            // 判斷 block 是否完全沒碰到 reserved range
            bool no_overlap = block_end <= start_pfn || pfn >= end_pfn;

            if (no_overlap) { // 不重疊
                ++it;
                continue;
            }

            // 走到這裡表示 block 至少有碰到 reserved range, 所以先移除。
            it = free_area[order].erase(it);

            // 判斷這個 block 是否完全落在 reserved range 裡
            bool full_overlap = start_pfn <= pfn && block_end <= end_pfn;

            if (full_overlap || order == 0) { // 如果 order == 0，代表已經切到最小單位 1 page, 即使只是 partial overlap，在 page allocator 裡也只能整頁 reserve
                page->order = order;
                page->refcount = 1;
            } else { // partial overlap
                // 解法：split 成兩個低一階的 buddy blocks，
                // 再把兩個 child blocks 放到 free_area[order - 1]。
                // 下一輪 order-- 時，會繼續檢查它們。

                unsigned int child_order = order - 1;

                // 每個子 block 的大小。
                // 例如 order 10 split 後變成兩個 order 9 blocks。
                // order 9 block 大小是 512 pages。
                size_t half = 1UL << child_order; // half is the 1lv lower block size
                
                struct page* left = page; // 左半邊 child block 的起點就是原本的 page

                struct page* right = &mem_map[pfn + half]; // 右半邊 child block 的起點是 pfn + half

                // 左半邊目前仍然先標成 free, 是否真的 free，等下一輪檢查 overlap 後才決定
                left->order = child_order;
                left->refcount = 0;

                right->order = child_order;
                right->refcount = 0;

                // 把兩個 child blocks 放到下一層 free_area。
                free_area[child_order].push_back(left);
                free_area[child_order].push_back(right);
            }
        }
    }
}

void dump() {
    for (int i = MAX_ORDER; i >= 0; i--)
        std::cout << "free_area[" << i << "] " << free_area[i].size() << std::endl;
}

// 初始化模擬的 memory allocator。
void mm_init() {
    mem_map.resize(NUM_PAGES); // 建立 NUM_PAGES 個 page metadata
    
    free_area.resize(MAX_ORDER + 1); // 建立 free_area[0] ~ free_area[MAX_ORDER]

    // 一開始假設整段 physical memory 都是 free
    //
    // 但不是一個 page 一個 page 放進 free_area[0]，
    // 而是切成最大 order 的 blocks，放進 free_area[MAX_ORDER]
    //
    // MAX_ORDER = 10
    // 每個 block 大小 = 2^10 = 1024 pages
    for (size_t i = 0; i < NUM_PAGES; i += (1 << MAX_ORDER)) {

        // mem_map[i] 是一個 order 10 block 的起始 page
        mem_map[i].order = MAX_ORDER;

        // 放入 free_area[10]。
        free_area[MAX_ORDER].push_back(&mem_map[i]);
    }

    // 保留 physical address range [0, 0x82a69510)
    //
    // 這代表從 physical address 0 開始，
    // 長度 0x82a69510 bytes 的範圍都不能被 Buddy System 分配
    memory_reserve(0, 0x82a69510);
}

int main() {
    mm_init();
    dump();

    return 0;
}