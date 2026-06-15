# EX1:
## asm volatile(
    "assembly code"
    : output operands
    : input operands
    : clobbers
);

asm volatile("mv tp, %0" : : "r"(idle_task) : "memory");
asm volatile("mv %0, tp" : "=r"(current)); *沒有 input operand / clobber，後面的冒號可省略

%0 代表第 0 個 input operand

"r"(idle_task)意思是：
請 compiler 把 idle_task 這個值放進某個 general-purpose register(e.g. mv a5, idle_task)
然後在 assembly 裡用 %0 代表它

"memory"意思是告訴 compiler：
這段 assembly 可能影響 memory 相關狀態，
不要把前後的 memory 操作亂重排。

## thread_trampoline
給 thread function 一個合法的 caller，並在 thread function return 後統一收尾。

thread_trampoline (跳板)用途是負責「第一次啟動 thread」(該某 function 內可能有 schedule() 會被切走)，之後 thread 被切走再切回是靠保存後的 ra/sp/s0~s11 回到原本位置，
以及確保 thread_exit() 能夠在某 function 執行完被執行到，因為該 function 沒 return 的話會 return 回 caller(i.e. thread_trampoline)。

因為 thread_create 並不是直接建立一條 thread、準備context 去執行某 function，是建立一條 thread 丟進 run_queue 等 schedule() 執行呼叫，
呼叫(switch_to 會做ld ra, 0(a1) // ra = next->context.ra = thread_trampoline)後 thread_trampoline 的 current->entry() 才真正去執行該某 function
