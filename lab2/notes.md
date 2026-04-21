# EX1:
做一個 bootloader(kernel.fit)，透過 UART 接收 host 傳來的 kernel image，將它載入到不會覆蓋原 bootloader 自己的位置(0x00200000)，可放在 0x20000000，然後 jump 過去執行；同時，被載入的 kernel 也必須 link 到那個位址

上電
→ Boot ROM → U-Boot SPL → OpenSBI → U-Boot
→ 你的 kernel.fit（第一階段 bootloader）
→ shell 顯示 prompt 輸入 load
→ bootloader 等待 UART data
→ host Python 傳 header + loader_target.bin
→ bootloader 收到 RAM 的高位址
→ bootloader jump 到新 image
→ 第二階段 kernel 開始執行


# EX2:
讓 kernel 不再依賴 non-portable 硬編碼硬體位址，而是從 devicetree 動態取得平台資訊，用 /soc/serial（OrangePi RV2）或 /soc/uart（QEMU）底下的 reg property 取得 UART base address.

DeviceTree描述:
1. 有哪些 device、樹狀結構中的位置
2. 每個 device 的 property
3. e.g. UART / memory / chosen / interrupt controller ...
├── cpus
│   └── cpu@0
│       └── interrupt-controller
├── memory@...
└── chosen
用 path 走到某一個 node，從 node 裡取 property


# EX3:
目前 kernel 還沒有 File system 跟 block device driver，無法直接從 SD 卡讀檔，所以先把檔案打包成 initramds.cpio(Initial Ramdisk) 放進 RAM，讓 kernel 有能力讀到一些檔案。

.cpio 是 New ASCII CPIO 格式，整個 archive 是一串 linear 封包化檔案：
[header][filename][padding][file data][padding]
[header][filename][padding][file data][padding]
...
[header]["TRAILER!!!"][padding]


# bare-metal kernel 裡不能用 host 版的 stdio.h、printf、strcmp、strlen 那些 libc，要自己寫簡單字串函式。