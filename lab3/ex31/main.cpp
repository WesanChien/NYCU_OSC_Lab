#include <iostream>
#include <vector>
#include <list>

#define NUM_PAGES 0x280000 // 2621440 pages
#define MAX_ORDER 10

struct page {
    int order = 0;
    int refcount = 0; // reference count: 1 -> allocated, 0 -> free
};

std::vector<struct page> mem_map; // mem_map[i] 代表第 i 個 physical page 的 metadata, the "i" a.k.a. PFN
std::vector<std::list<struct page*>> free_area; // free_area[x] 裡面放 order x 的 free blocks 起始 page，也就是 2^x pages blocks

struct page* get_buddy(struct page* page, unsigned int order) {
    size_t pfn = page - mem_map.data(); // page addr. - first element addr. of the whole vector(pointer arithmetic in C/C++)
    size_t block_size = 1UL << order;
    size_t buddy_pfn = pfn ^ block_size; // buddy 的 PFN 可以用 XOR 算出來。=

    return &mem_map[buddy_pfn];
}

struct page* alloc_pages(unsigned int order) {
    if (order > MAX_ORDER)
        return nullptr;

    for (unsigned int current_order = order; current_order <= MAX_ORDER; ++current_order) {
        if (!free_area[current_order].empty()) {
            struct page* page = free_area[current_order].front(); // page 指向這個 free list 第一個 block 的起始 page
            free_area[current_order].pop_front(); // remove this block from free_area

            while (current_order > order) { // 如果找到的 block 比要求的還大，就要 split 'til the same as requested block size
                current_order--;

                size_t pfn = page - mem_map.data(); // split 後左半邊 block 起點
                size_t buddy_pfn = pfn + (1UL << current_order); // 右半邊 buddy 的 block 起點
                struct page* buddy = &mem_map[buddy_pfn]; // 右半邊 buddy block 的起始 page metadata

                buddy->order = current_order; // 這個 buddy 是 split 出來但沒有被配置出去的 block, 所以它的 order 是 current_order
                buddy->refcount = 0;
                free_area[current_order].push_back(buddy); // 把右半邊 buddy 放回對應 order 的 free list。
            }

            page->order = order;
            page->refcount = 1;
            return page; // 回傳配置成功的 block 起點
        }
    }
    return nullptr;
}

void free_pages(struct page* page) {
    if (!page || page->refcount == 0)
        return;

    size_t pfn = page - mem_map.data();
    unsigned int order = page->order;

    page->refcount = 0;

    while (order < MAX_ORDER) { // 從目前 order 一路往上 merge
        size_t buddy_pfn = pfn ^ (1UL << order);
        
        if (buddy_pfn >= NUM_PAGES) // 安全檢查：buddy 不應該超出 mem_map 範圍
            break;

        struct page* buddy = &mem_map[buddy_pfn];

        if (buddy->refcount != 0 || buddy->order != order) { // buddy must be free and same order as current block order
            break;
        }

        free_area[order].remove(buddy);

        if (buddy_pfn < pfn) { // merge 之後的新 block 起點，要取兩者中較低的 PFN
            page = buddy;
            pfn = buddy_pfn;
        }
        order++;
        page->order = order;
    }
    
    free_area[order].push_back(page); // merge 結束後，把最終形成的 free block 放入對應 order 的 free list
}

void dump() { // 印出每個 order 的 free block 數量。
    for (int i = MAX_ORDER; i >= 0; i--)
        std::cout << "free_area[" << i << "] " << free_area[i].size() << std::endl;
}
int main() {
    mem_map.resize(NUM_PAGES);
    free_area.resize(MAX_ORDER + 1);
    for (size_t i = 0; i < NUM_PAGES; i += (1 << MAX_ORDER)) {
        mem_map[i].order = MAX_ORDER;
        free_area[MAX_ORDER].push_back(&mem_map[i]);
    }

    std::cout << "\np1:\n";
    struct page* p1 = alloc_pages(1);
    dump();

    std::cout << "\np2:\n";
    struct page* p2 = alloc_pages(1);
    dump();

    std::cout << "\np3:\n";
    struct page* p3 = alloc_pages(1);
    dump();

    free_pages(p1);
    free_pages(p2);
    free_pages(p3);

    std::cout << "\nfree:\n";
    dump();
    return 0;
}