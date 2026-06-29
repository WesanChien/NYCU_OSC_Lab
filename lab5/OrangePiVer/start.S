.section ".text.boot"
.global _start

_start:
    // Clear the .bss section
    la   t0, __bss_start
    la   t1, __bss_stop

bss_clear:
    bgeu t0, t1, bss_done
    sb   zero, 0(t0)
    addi t0, t0, 1
    j    bss_clear

bss_done:
    // Set up the stack pointer (stack grows downward from _end)
    la   sp, _end

    // Firmware passes DTB address in a1, move it to a0 as C arg0 to start_kernel(unsigned long fdt_addr)
    mv   a0, a1
    
    tail start_kernel

.section .text
.globl handle_exception
.align 2

.equ TF_SIZE, 280
.equ SSTATUS_SPP, 0x100

handle_exception:
    /*
     * 判斷 trap 發生前是 U-mode 還是 S-mode。
     *
     * trap 進 S-mode 後，硬體會把「trap 前的 privilege mode」
     * 記錄到 sstatus.SPP。
     *
     * SPP = 0：trap 前是 U-mode
     * SPP = 1：trap 前是 S-mode
     */
    csrr t0, sstatus
    andi t0, t0, SSTATUS_SPP
    bnez t0, 1f
    
    /*
     * 只有 U-mode trap 才需要切 stack
     * trap 從 U-mode 進來時：
     *     sp       = user stack
     *     sscratch = kernel stack
     *
     * csrrw sp, sscratch, sp 會交換 sp 和 sscratch。
     *
     * 交換後：
     *     sp       = kernel stack
     *     sscratch = user stack
     * 這樣 kernel 才能安全地把 trap frame 存到 kernel stack。
     */
    csrrw sp, sscratch, sp 

    1:
    /*
     * 現在 sp 一定是 kernel stack。
     * 可以安全建立 trap frame。
     */
    addi sp, sp, -TF_SIZE

    sd ra, 0 * 8(sp)

    /*
     * 先保存原本 t0
     * 因為前面 t0 已經被拿來判斷 SPP，所以這裡保存的是目前 t0，
     * 對 S-mode interrupt 來說可能不是完全乾淨
     *
     * 為了更嚴謹，實務上會一進 trap 就先用 scratch register 或更完整保存
     */
    sd t0, 4 * 8(sp)

    /*
     * 保存 interrupted sp
     *
     * 如果 trap from U-mode： sscratch 是 user sp
     *
     * 如果 trap from S-mode：
     *     interrupted sp 就是目前 trap frame 上方的位置，也就是 sp + TF_SIZE
     */
    csrr t0, sstatus
    andi t0, t0, SSTATUS_SPP
    bnez t0, 2f

    /*
     * U-mode trap：user sp 在 sscratch
     */
    csrr t0, sscratch
    sd t0, 1 * 8(sp)
    j 3f

2:
    /*
     * S-mode trap：原本的 kernel sp 是目前 sp + TF_SIZE。
     */
    addi t0, sp, TF_SIZE
    sd t0, 1 * 8(sp)

3:
    sd gp, 2 * 8(sp)
    sd tp, 3 * 8(sp)

    sd t1, 5 * 8(sp)
    sd t2, 6 * 8(sp)
    sd s0, 7 * 8(sp)
    sd s1, 8 * 8(sp)
    sd a0, 9 * 8(sp)
    sd a1, 10 * 8(sp)
    sd a2, 11 * 8(sp)
    sd a3, 12 * 8(sp)
    sd a4, 13 * 8(sp)
    sd a5, 14 * 8(sp)
    sd a6, 15 * 8(sp)
    sd a7, 16 * 8(sp)
    sd s2, 17 * 8(sp)
    sd s3, 18 * 8(sp)
    sd s4, 19 * 8(sp)
    sd s5, 20 * 8(sp)
    sd s6, 21 * 8(sp)
    sd s7, 22 * 8(sp)
    sd s8, 23 * 8(sp)
    sd s9, 24 * 8(sp)
    sd s10, 25 * 8(sp)
    sd s11, 26 * 8(sp)
    sd t3, 27 * 8(sp)
    sd t4, 28 * 8(sp)
    sd t5, 29 * 8(sp)
    sd t6, 30 * 8(sp)

    csrr t0, sepc
    sd t0, 31 * 8(sp)

    csrr t0, sstatus
    sd t0, 32 * 8(sp)

    csrr t0, scause
    sd t0, 33 * 8(sp)

    csrr t0, stval
    sd t0, 34 * 8(sp)

    /*
     * do_trap(struct trap_frame *tf)
     */
    mv a0, sp
    call do_trap

    /*
     * 還原 CSR
     * do_trap 可能修改 tf->sepc。
     */
    ld t0, 31 * 8(sp)
    csrw sepc, t0

    ld t0, 32 * 8(sp)
    csrw sstatus, t0

     /*
     * 重要：
     * sscratch 是 per-hart CSR，不是 per-task。
     * 如果在 syscall handler 裡 schedule() 到其他 user process，
     * sscratch 可能已經被其他 process 的 trap 改掉。
     *
     * 所以回去前必須從目前 trap_frame 重新載入 interrupted sp。
     *
     * U-mode trap:
     *     tf->sp = user sp
     *
     * S-mode trap:
     *     tf->sp = 原本 kernel sp
     */
    ld t0, 1 * 8(sp)
    csrw sscratch, t0

    /*
     * 還原 general registers
     */
    ld ra, 0 * 8(sp)
    ld gp, 2 * 8(sp)
    ld tp, 3 * 8(sp)
    ld t0, 4 * 8(sp)
    ld t1, 5 * 8(sp)
    ld t2, 6 * 8(sp)
    ld s0, 7 * 8(sp)
    ld s1, 8 * 8(sp)
    ld a0, 9 * 8(sp)
    ld a1, 10 * 8(sp)
    ld a2, 11 * 8(sp)
    ld a3, 12 * 8(sp)
    ld a4, 13 * 8(sp)
    ld a5, 14 * 8(sp)
    ld a6, 15 * 8(sp)
    ld a7, 16 * 8(sp)
    ld s2, 17 * 8(sp)
    ld s3, 18 * 8(sp)
    ld s4, 19 * 8(sp)
    ld s5, 20 * 8(sp)
    ld s6, 21 * 8(sp)
    ld s7, 22 * 8(sp)
    ld s8, 23 * 8(sp)
    ld s9, 24 * 8(sp)
    ld s10, 25 * 8(sp)
    ld s11, 26 * 8(sp)
    ld t3, 27 * 8(sp)
    ld t4, 28 * 8(sp)
    ld t5, 29 * 8(sp)
    ld t6, 30 * 8(sp)

    /*
     * 釋放 trap frame。
     */
    addi sp, sp, TF_SIZE

    /*
     * 如果原本是 U-mode trap，回去前要把 stack 換回 user stack。
     *
     * 交換前：
     *     sp       = kernel stack
     *     sscratch = user stack
     *
     * 交換後：
     *     sp       = user stack
     *     sscratch = kernel stack
     */
    csrrw sp, sscratch, sp
    sret

.global switch_to
switch_to:
    /*
     * a0 = prev task_struct *
     * a1 = next task_struct *
     *
     * task_struct 開頭就是 cpu_context：
     *   0(a0)   ra
     *   8(a0)   sp
     *   16(a0)  s0
     *   ...
     *   104(a0) s11
     */

    /* Save prev context */
    sd ra,   0(a0)
    sd sp,   8(a0)
    sd s0,  16(a0)
    sd s1,  24(a0)
    sd s2,  32(a0)
    sd s3,  40(a0)
    sd s4,  48(a0)
    sd s5,  56(a0)
    sd s6,  64(a0)
    sd s7,  72(a0)
    sd s8,  80(a0)
    sd s9,  88(a0)
    sd s10, 96(a0)
    sd s11, 104(a0)

    /* Restore next context */
    ld ra,   0(a1)
    ld sp,   8(a1)
    ld s0,  16(a1)
    ld s1,  24(a1)
    ld s2,  32(a1)
    ld s3,  40(a1)
    ld s4,  48(a1)
    ld s5,  56(a1)
    ld s6,  64(a1)
    ld s7,  72(a1)
    ld s8,  80(a1)
    ld s9,  88(a1)
    ld s10, 96(a1)
    ld s11, 104(a1)

    /*
     * tp = current task_struct *
     */
    mv tp, a1

    ret