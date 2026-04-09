.section ".text.boot"
.global _start

_start:
    # clear .bss # Setup the range of bss section
    la t0, __bss_start 
    la t1, __bss_stop

1:
    bgeu t0, t1, 2f # if t0 >= t1 branch to forward label2 (Stop setting bss section zero)
    sd zero, 0(t0) # store doubleword(8 bytes) 0 to t0, aka 以 8-byte 為單位清 .bss
    addi t0, t0, 8 # 把 t0 往後移 8 bytes
    j 1b # Jump to backward label1, keep doing this until t0 addr. is the same as t1 addr., that means all bss section is set to 0 already

2:
    # setup stack pointer
    la sp, _end # 把 stack pointer 設到 linker 給你的記憶體尾端 (0x80204000), 因為 stack 要往低位址長

    tail start_kernel # 跳進 C 的 start_kernel()