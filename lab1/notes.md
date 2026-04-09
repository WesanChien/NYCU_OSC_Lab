
# Basic Initialization
## Why clearing BSS section to 0
BSS 要 kernel 把它清成 0，是因為 bare-metal 啟動時，沒有人幫你自動清 .bss，不像一般 user-space 程式有 loader 會幫你做。
所以要手刻清理的 code，避免未初始化 global variable 是垃圾值。

## Why setting up Stack Pointer
Stack Pointer 一定要先設定，因為只要一進入 C 函式，Compiler 通常就會預期 stack 已經能用。
 C 函式可能會存 return address、存 local variables、建立 stack frame
如果 sp 沒設好，就可能寫到錯誤位址、覆蓋 kernel code/data、直接 exception 或 hang。

# UART Setup
RISC-V 用 Memory Mapped I/O

# 硬體差異
在 QEMU 模擬是 8-bit UART register，每個位址就是一個 byte register。
OrangePi 是 32-bit memory-mapped register，register 本身是用 word access