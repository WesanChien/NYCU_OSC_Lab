# EX1:
#### kernel thread 
只跑在 S-mode,
每個 thread 有自己的 kernel stack,
thread 之間靠 switch_to() 切換

#### user process 
task struct + user/kernel stack + trap frame + 一個 kernel thread 外殼 user_process_entry(),
但實際 user program 會透過 sret 進入 U-mode 執行

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

## 目前 kernel 整體可以分成三層:
最底層：context switch
    switch_to()
    保存/還原 ra, sp, s0~s11
    更新 tp = next task

中間層：task / scheduler
    task_struct
    run queue
    schedule()
    thread_create()
    thread_exit()
    task_reap()
    task_kill()

上層：user process / syscall
    user_process_create()
    user_process_entry()
    trap_frame
    ecall
    syscall_handler()
    fork/waitpid/exit/stop/exec

## 不是直接把 user_program() 當 kernel thread 跑，而是：
    thread_create(user_process_entry)
        ↓
    user_process_entry() 在 S-mode 執行
        ↓
    設定 sscratch = current->kernel_sp
        ↓
    return_to_user(&current->trapframe)
        ↓
    sret
        ↓
    CPU 進入 U-mode
        ↓
    開始執行真正的 user program

所以 user process 的啟動分兩段：
S-mode kernel 外殼：user_process_entry()
U-mode 真正程式：user_program / user_fork_test / user_stop_test / user_exec_test

## 目前 Lab5 的實作中：
task_struct 是 scheduler 管理的基本執行單位。
一開始它比較像 kernel thread。
後來加上 trap_frame、user stack、parent、exit_status 之後，
它也被拿來表示 user process。

## exec from initrd 流程：
kernel shell:
    exec osctest.bin
        ↓
shell.c:
    user_process_create_from_file("osctest.bin")
        ↓
user.c:
    user_load_image_from_initrd("osctest.bin", &entry)
        ↓
user_loader.c:
    initrd_find_file("osctest.bin", &file_data, &file_size)
        ↓
initrd.c:
    掃描 cpio newc archive，找到 osctest.bin
        ↓
user_loader.c:
    copy file_data 到 USER_PROG_BASE = 0x03000000
    fence.i
    entry = USER_PROG_BASE
        ↓
user.c:
    建立 task_struct
    設定 user stack
    設定 trapframe.sepc = entry
        ↓
scheduler:
    schedule 到該 task
        ↓
user_process_entry()
    設定 sscratch = kernel_sp
    return_to_user(&trapframe)
        ↓
sret
        ↓
CPU 進 U-mode
        ↓
開始執行 osctest.bin

# EX3
## timer 的修改：從「software timer」變成「periodic tick + software timer」

你原本 Lab4 的 timer 是 software timer queue：

add_timer(callback, arg, sec)
    ↓
把 timer_event 插進 sorted linked list
    ↓
hardware timer 設成最早到期的 event
    ↓
到期後執行 callback

"如果沒有 software timer，就沒有必要一直進 timer interrupt"

### EX3 需要：

就算沒有 add_timer event，
kernel 也要每 1/32 秒收到 timer interrupt，
用來 preempt user process。

所以把 timer 改成同時管理：

1. software timer event
   timer_head->expires_at

2. scheduler tick
   sched_next_tick

### 這樣就算沒有任何 add_timer() event，timer interrupt 也會因為 sched_next_tick 繼續發生。

## sys_uart_read 更動

一開始的 sys_uart_read() 是 uart_getc(),它是 blocking function。
沒有輸入時，它會一直等, 一直卡在 kernel S-mode 裡

情況變成：

user shell 等輸入
    ↓
ecall SYS_UART_READ
    ↓
kernel 進 sys_uart_read()
    ↓
uart_getc() blocking
    ↓
CPU 卡在 S-mode
    ↓
timer interrupt 來了，但 SPP=1，不 schedule
    ↓
video child 沒機會跑

### 因此新增 non-blocking API：int uart_try_getc(char *out)

# Advanced EX1
    OrangePi RV2> exec osctest.bin (U-mode)(osctest.bin 內原本就有一個 signal handler 函式)
    $ signal (發起 SYS_SIGNAL 把 SIGTERM = 15 與該 handler function 的位址登記到 task_struct.signal_handlers[15])
    $ fork (child 繼承 parent task_struct 內已登記的 handler)
    child pid: 2
    $ kill 2 (SYS_KILL 請 kernel 對 pid 2 傳送 SIGTERM(15), 但 Kernel 發現 signal_handlers[15] 有 handler, 不採用預設終止行為, 設定 pending_signals bit 15 = 1, 表示這個 process 有一個 SIGTERM 尚未處理, 返回 parent, child 下一次因 interrupt、syscall 或其他進入 kernel，而且準備回 U-mode 前，kernel 呼叫：signal_try_deliver(tf);裡面會找 pending signal)

流程：
    SYS_SIGNAL
        ↓
    把 SIGTERM=15 對應的 handler address
    存到 current->signal_handlers[15]

    fork
        ↓
    child 繼承 parent 的 signal_handlers[]

    SYS_KILL(pid=2, signum=15)
        ↓
    檢查 child->signal_handlers[15]
        ↓
    有 handler
        ↓
    不採用 SIGTERM 的預設終止行為
        ↓
    child->pending_signals 的 bit 15 設成 1

    child 準備回 U-mode
        ↓
    kernel 發現 pending bit 15
        ↓
    清除 pending bit
        ↓
    保存原本 trap frame
        ↓
    切到 signal stack
        ↓
    把 sepc 改成 handler address, ra = signal trampoline, sp = signal stack, a0 = SIGTERM
        ↓
    sret 執行 handler

    handler return
        ↓
    跳到 signal trampoline
        ↓
    SYS_SIGRETURN
        ↓
    恢復原本 trap frame
        ↓
    child 繼續執行

Note *為什麼 kill 後不是立刻執行 handler？(概念並不是 SYS_EXEC)
假設 parent(shell) 正在執行：
    kill 2
此時 current 是 parent，不是 child。
kernel 只能把 signal 記到 child：
    child->pending_signals |= 1UL << 15;
等 scheduler 選到 child，child 透過 timer interrupt或 syscall 進 kernel時，get_current() 才會是 child。
這時才能安全改 child 自己的 trap frame。
