#  Linux 記憶體階層
PGD, P4D, PUD, PMD, PTE, Page
最低的 12-Bits 作為 Page 的 Offset(4KB page)
剩下的 Bits 會分成四 or 五個階層，每個階層 9-Bits

# Ex61 
ex61 使用 2 MiB huge page，only PGD, PMD 所以結構是：
PGD entry
    ↓
PMD table
    ↓
PMD leaf entry
    ↓
2 MiB physical RAM

ex61:
pgd[512];       可處理 512 GB Virtual Memory Address
pmd[4][512];    可處理 4 * 1 GB Physical Memory Address
實際 RAM:        0x80000000 起的 4 GB
identity VA:    4 GB VA 映射到這 4 GB PA
higher-half VA: 另一段 4 GB VA 映射到同一份 4 GB PA

因此實際建立的是(每張 table 皆 4KB, cuz PTE 8 byte * 512)：
1 張 PGD table
4 張 PMD table
0 張 PTE table

## MAKE_PTE() 在做什麼
#define MAKE_PTE(pa, flags) ((((unsigned long)(pa)) >> 12) << 10 | (flags))
RISC-V PTE 不是直接存完整 physical address。
所以先移除 PA 的 page offset：pa >> 12
得到 Physical Page Number，PPN。
接著把 PPN 放到 PTE 的 bit 10 以上：(pa >> 12) << 10
最後 bitwise OR 加入 flags：... | flags

## 如何分辨 non-leaf 和 leaf
只有 V：PTE_V
代表它不是最終 mapping，而是指向下一張 page table：
PGD entry → PMD table
若包含 V/R/W/X：PTE_V | PTE_R | PTE_W | PTE_X
代表它是 leaf，直接映射實體記憶體：
PMD entry → 2 MiB physical RAM

## satp register
Supervisor Address Translation and Protection
啟用虛擬記憶體轉換，並指向系統 root page table 的 PA 實體位置

## Sv39 的虛擬位址空間通常分成：
低位址：User space
高位址：Kernel space

完整 OS 還要加入 user process。希望配置成：
低位 VA：每個 process 自己的程式、data、stack
高位 VA：所有 process 共用的 kernel

e.g.
User process
VA 0x0000000000000000
VA 0x0000004000000000
          ...
──────────────────────────
Kernel
VA 0xffffffc000000000
VA 0xffffffc080200000

## 為什麼需要 2 種 mapping (higher-half 跟 identity)?
啟用 MMU 的瞬間, 假設執行 setup_vm() 時，CPU 正在實體位址執行,
目前 PC = 0x80200100, 然後執行：csrw satp, ... 開啟 MMU 了,
但 CPU 不會自動把 PC 變成：0xffffffc080200100,
下一條指令的 PC 仍然是：0x80200104,
只是從現在開始，CPU 把它當成 VA，而不是 PA,
CPU 會查不到 mapping PA。

RAM 和程式沒有複製, 只是 page table 提供兩條不同的入口。

#### 因此 "Identity mapping 只是過渡用的橋"
MMU 剛開啟時，維持低位址 PC 暫時可用,
higer-half mapping 才是 kernel 最後永久使用的 VA