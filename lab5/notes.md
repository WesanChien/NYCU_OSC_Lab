# EX1:
kernel thread 只跑在 S-mode,
每個 thread 有自己的 kernel stack,
thread 之間靠 switch_to() 切換
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


# EX2:
user process 有：
1. kernel stack
(user process 在 U-mode 跑的時候，sp 是 user stack, 但發生 trap, kernel 不能繼續用 user stack，必須切回 kernel stack, 所以會用：
    asm volatile("csrw sscratch, %0" : : "r"(current->kernel_sp));
把 kernel stack top 放進 sscratch, 之後 trap 進來時，assembly 會交換成：
    sp       = kernel stack
    sscratch = user stack
這樣 kernel 才能安全地在 kernel stack 上建立 trap frame)
2. user stack
3. trap frame
4. user-mode entry point
5. syscall 能力
### thread_create(user_process_entry);
user_program 不能直接用 S-mode 的普通 function call 方式執行。
如果 thread_create(user_program);
那它會在 S-mode 執行，這就不是 user process。

流程是：
    先建立一個 kernel thread
        ↓
    這個 kernel thread 的 entry 是 user_process_entry
        ↓
    user_process_entry 準備 sscratch
        ↓
    return_to_user(&trapframe)
        ↓
    sret 進 U-mode
        ↓
    真正開始跑 user_program

### ecall 發生時
RISC-V 硬體會做幾件事：
1. privilege 從 U-mode 進入 S-mode
2. scause = 8，也就是 Environment call from U-mode
3. sepc = ecall 指令的位置
4. sstatus.SPP = 0，代表 trap 前是 U-mode
5. 跳到 stvec 指向的 trap entry