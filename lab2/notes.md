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

## DeviceTree 描述:
1. 有哪些 device、樹狀結構中的位址
2. 每個 device 的 property
3. UART / memory / chosen / interrupt controller 在哪
├── cpus
│   └── cpu@0
│       └── interrupt-controller
├── memory@...
└── chosen
4. 用 path 走到某一個 node，從 node 裡取 property
5. initrd 被放在哪裡(initrd-start, initrd-end address)

(e.g. x1_orangepi-rv2.dtb 代表這份 device tree 是針對 Orange Pi RV2 / X1 這個 board 的硬體配置)
### kernel 從 DTB 讀出這些資訊, 這樣同一份 kernel 跑在不同 board 上，只需換不同 .dtb


# EX3:
目前 kernel 還沒有 File system 跟 block device driver，無法直接從 SD 卡讀檔，所以先把檔案打包成 initramds.cpio(Initial Ramdisk) 放進 RAM，
用來提供 early boot 所需的基本檔案與 driver, 讓 kernel 有能力讀到一些檔案。

.cpio 是 New ASCII CPIO 格式，整個 archive 是一串 linear 封包化檔案：
[header][filename][padding][file data][padding]
[header][filename][padding][file data][padding]
...
[header]["TRAILER!!!"][padding]

## rootfs 是 root filesystem，也就是系統開機後看到的 / 根目錄內容。
一般 Linux 開機後有：
/
├── bin
├── sbin
├── etc
├── dev
├── proc
├── sys
├── usr
├── home
└── tmp

### 目前沒有 Filesystem，所以用 Initrd 當成暫時的 root file system (loaded into memory)
這整包檔案系統內容就可以叫 rootfs。

課程範例的rootfs/
├── hello.txt
└── osc.txt

然後用 cpio 打包成：initramfs.cpio

這個 initramfs.cpio 之後會被載入 RAM，這段 memory address 就會被當成 initial ramdisk 使用,
kernel 在 early boot 階段可以透過 initrd_start / initrd_end 找到它，並解析裡面的檔案。

# bare-metal kernel 裡不能用 host 版的 stdio.h、printf、strcmp、strlen 那些 libc，要自己寫簡單字串函式。