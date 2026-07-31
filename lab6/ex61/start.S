.section ".text.boot"
.global _start
_start:
    /* 算出 label 1 的 higher-half virtual address
     *
     * 1f 的意思是：往 forward 方向尋找下一個名 local label 1
     * 假設 label 1 的實體位址是：0x80200050
     * 加上 PAGE_OFFSET： 0xffffffc080200050
     * 存入 s0，稍後用來跳到 higher half
     */
    la      s0, 1f
    li      t0, 0xffffffc000000000
    add     s0, s0, t0

    /* MMU 尚未開啟，此時 sp 必須是 physical address
     *
     * 呼叫 C 函式前必須先有 stack，因為 C 函式可能需要：
     * 保存 register, ra, 建立 local variable
     * 此時 MMU 還沒開，所以 sp 必須能在實體位址下使用
     */
    la      sp, _end

    /* 建立 page table 並開啟 Sv39 */
    call    setup_vm

    /* MMU 開啟後，跳到 higher-half VA */
    jr      s0

.align 2
1:
    /* 現在 PC 已位於 higher half，重新取得 virtual stack address */
    la      sp, _end
    la      a3, __bss_start
    la      a4, __bss_stop
    ble     a4, a3, clear_bss_done
clear_bss:
    sd      zero, (a3)
    add     a3, a3, 8
    blt     a3, a4, clear_bss
clear_bss_done:
    call    drop_identity_map
    tail    start_kernel