# ex41
## CSRs:
1. sstatus (Supervisor Status Register)
控制、追蹤當前 CPU 的執行狀態。
核心欄位：
SIE (Supervisor Interrupt Enable)：控制 S-mode 當前是否允許中斷。
SPIE (Supervisor Previous Interrupt Enable)：儲存進入 Trap 前的 SIE 狀態。
STIE (Supervisor Timer Interrupt Enable)
SPP (Supervisor Previous Privilege)：紀錄進入 Trap 前的特權模式（0 表 U-mode，1 表 S-mode）。

2. stvec (Supervisor Trap Vector Base Address Register)
決定 Trap 發生時的硬體跳躍目標。
核心欄位：
BASE (位元 [XLEN-1:2])：Trap 處理程式的基底地址（必須 4 字節對齊）。
MODE (位元 [1:0])：
00 (Direct)：所有異常與中斷皆跳躍至 BASE。
01 (Vectored)：異常跳躍至 BASE；中斷則跳躍至 BASE + (Async_Cause × 4)。
硬體行為： 發生 Trap 時，處理器核心（Core）會自動將 pc 暫存器強制更新為由 stvec 計算出的目標地址。

3. sepc (Supervisor Exception Program Counter)
記錄觸發 Trap 的 Instruction address
硬體行為（進入 Trap）： 硬體自動將當前受阻（或觸發異常）的指令 pc 寫入 sepc。若為同步異常（Exception），sepc 指向觸發異常的指令本身；若為非同步中斷（Interrupt），sepc 指向下一條尚未執行的指令。
硬體行為（執行 sret）： 處理器會自動將 pc 載入為 sepc 中的值，返回原程式流。

4. scause (Supervisor Cause Register)
記錄觸發 Trap 的具體原因。
核心欄位：
Interrupt (最高位元 Index)：1 表示非同步中斷（如定時器中斷），0 表示同步異常（如記憶體對齊錯誤、Page Fault）。
Exception Code (其餘位元)：對應規格書的編碼表（例如：編碼 9 代表 U-mode Ecall；編碼 13 代表 Load Page Fault）。
硬體行為： 硬體在 Trap 發生瞬間，自動寫入對應的最高位元與錯誤代碼，供軟體 Handler 進行分支判斷。

5. stval (Supervisor Trap Value Register)
提供處理異常所需的附加輔助資訊。
硬體行為：
發生 Page Fault 或 Access Fault 時，硬體自動將**觸發錯誤的虛擬記憶體地址（VA）**寫入 stval。
發生 非法指令（Illegal Instruction） 時，硬體會將該**錯誤指令的二進位碼（Opcode）**寫入 stval。
其他情況（如外部中斷），硬體通常將其清零。

6. sscratch (Supervisor Scratch Register)
刮 trace / 專用暫存器，用於上下文切換的互換媒介。
核心角色： 專門供軟體使用的暫存空間。在 S-mode 下，通常用來儲存指向「核心棧（Kernel Stack）」或「處理器控制塊（Thread/CPU Context Block）」的指標。
硬體行為： 硬體本身不主動修改此暫存器。
軟體常規用法： 進入 Trap 後，軟體第一步會使用 csrrw sp, sscratch, sp 指令。這能在一條指令內，完成「將用戶棧指標（User SP）存入 sscratch」並「切換為內核棧指標（Kernel SP）」的原子操作，避免破壞通用暫存器。

7. sie (Supervisor Interrupt Enable Register)
控制各個獨立中斷源的開啟與關閉。
核心欄位：SEIE (Supervisor External Interrupt Enable)、STIE (Supervisor Timer Interrupt Enable)、SSIE (Supervisor Software Interrupt Enable)
硬體行為： 只有當 sstatus.SIE = 1 且 sie 中對應的特定中斷位元亦為 1 時，該中斷源才能成功觸發處理器進入 Trap。

# ex42
## Flow:
1. trap 進來
2. kernel 讀 PLIC_CLAIM
3. PLIC 回傳目前最高 priority 的 pending IRQ
4. kernel 處理 IRQ
5. kernel 把同一個 IRQ number 寫回 PLIC_CLAIM
6. PLIC gateway 解鎖，之後同一個 device 才能再送 interrupt

# Basic EX1
user program 主動 ecall

trap 發生時，CPU 只會幫你切到 S-mode 並跳到 stvec (run Trap Handler)
CPU 不會自動幫你保存所有 register, so you have to do it in start.S by yourself(handle_exception)

# Basic EX2
硬體 timer 到時間後會自動讓 CPU 進入 trap handler

sbi_set_timer(next_time)
不是設定「2 秒後」這種 relative delay，而是設定：
當 time CSR 到達 next_time 時，產生 timer interrupt

# Basic EX3
UART 收到字元
  -> UART hardware 產生 interrupt
  -> PLIC 收到 UART IRQ
  -> CPU trap，scause = external interrupt
  -> kernel 讀 PLIC claim
  -> 發現 IRQ = UART0_IRQ_ID
  -> uart_handle_irq()
      -> RX：把 UART RBR 的字元搬到 rx ring buffer
      -> TX：如果 UART 可送，就從 tx ring buffer 搬字元到 THR
  -> PLIC complete
  -> 回到原本程式

UART interrupt 是外部裝置，所以中間會多一個 PLIC, 負責管理「外部裝置 interrupt」，例如：UART, GPIO, SPI, I2C, 
對 CPU 來說，外部 interrupt 只會看到：scause = 0x8000000000000009 (a.k.a. Supervisor External Interrupt)
但 CPU 不知道是哪個裝置造成的，所以要問 PLIC：irq = plic_claim(); (e.g. IRQ 42), 
然後 kernel 才知道是 UART0 (IRQ 42)

流程是：
CPU 收到 external interrupt
  -> plic_claim()
  -> 回傳 42
  -> 代表 UART0 interrupt
  -> uart_handle_irq()
  -> plic_complete(42)

*(volatile unsigned int *) 0xE0000000 是 PLIC 的某個 register(MMIO)

假設你按下鍵盤 h, 完整流程:
1. UART hardware 收到 'h'
2. UART 設定內部狀態：LSR_DR = 1, 代表 Receive Buffer 有資料
3. UART 因為 RX interrupt enable，所以送出 interrupt signal
4. PLIC 收到 UART0 IRQ 42
5. CPU trap
6. scause = external interrupt
7. trap.c 呼叫 plic_claim()
8. plic_claim() 回傳 42
9. trap.c 呼叫 uart_handle_irq()
10. uart_handle_irq() 從 RBR 讀 'h'
11. 存進 rx_buf[rx_w]
12. plic_complete(42)
13. 回到 shell
14. shell 的 uart_getc() 從 rx_buf 取出 'h'

plic.c 只負責一件事：設定 PLIC，並提供 claim / complete 介面。
只負責 interrupt routing。