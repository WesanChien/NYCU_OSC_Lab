# EX1, EX2:
當 request size 小於一個 page size，就走 Dynamic Memory Allocator(Chunk allocator)，把 page 切成 chunk，當 chunk pool 沒空間，就再向 Buddy System 要一個 page，全部切成該 size。
當超過一個 page size，就走 Buddy System( Page Frame Allocator)，依所需 pages 數換成 order 來分配連續 pages。

使用者呼叫：
    allocate(size)
    free(ptr)

底層分成兩種 allocator：

1. Page Frame Allocator
   - alloc_pages(order)
   - free_pages(addr)
   - 管理單位是 page
   - 適合配置一個 page 或多個連續 pages

2. Dynamic Memory Allocator
   - alloc_chunk(size)
   - free_chunk(ptr)
   - 管理單位是 chunk
   - 適合配置 16 / 32 / 64 / 128 bytes 這種小空間

# Advanced EX2
把原本 hardcode memory range(0x10000000~0x20000000) 改成由 bootloader 傳進來的 DTB 的 /memory node 決定可用 memory range 給 allocator 使用(managed_base / managed_size)，且避開 reserved mem addr. for DTB 自己, kernel image, initramfs...

流程:
1. 從 DTB 找出「整塊 RAM 的範圍」
2. 把這段 RAM 設成 allocator 可以管理的範圍
3. 初始化 buddy system，先假設這整段 RAM 都可以分配
4. 再把已經被使用的區域 reserve 起來
   - kernel image
   - DTB 自己
   - initramfs
5. 之後 allocate() 就只能從剩下的 free memory 裡面拿空間

*fdt:
U-Boot 啟動 kernel 時傳給 start.S，
start.S 再傳給 start_kernel(fdt_addr)，
main.c 把 fdt_addr 轉成 fdt 指標

# Advanced EX3
原本是預先在 .bss 裡保留一大塊 metadata 空間(static struct page frame_array[MAX_FRAME_COUNT];), 改成實際有多少 physical pages，就在開機時配置多少 struct page metadata.
"frame_array 從 static array 改成由 startup allocator 配置出來的 memory"

在正式 Buddy System 還不能使用之前，
用一個很簡單的 startup allocator，
避開 kernel / DTB / initramfs，
先配置出 frame_array，
再把 frame_array 本身也 reserve 起來，
最後才建立完整 Buddy System。

mm_init() 流程:
1. 從 DTB 讀 /memory
2. 設定 managed_base / managed_size / frame_count
3. early_reserve_add(Kernel)
4. early_reserve_add(DTB)
5. early_reserve_add(Initramfs)
6. startup_allocator_init()
7. startup_alloc(frame_array_size)
8. early_reserve_add(FrameArray)
9. 初始化 free_area
10. 初始化 frame_array metadata
11. 把整段 managed memory 加入 buddy system
12. 對所有 early_reserved regions 呼叫 memory_reserve()
