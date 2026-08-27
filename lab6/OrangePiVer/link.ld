OUTPUT_ARCH(riscv)
ENTRY(_start)

SECTIONS
{
    /*
     * FIT image 仍把 raw binary 載入 physical 0x00200000，
     * 但 kernel symbol 的 link-time VA 改成 higher-half。
     */
    . = 0xffffffc000000000;
    . += 0x00200000;

    __kernel_start = .;

    .text : {
        *(.text.boot)
        *(.text)
        *(.text.*)
    }

    .rodata : {
        *(.rodata)
        *(.rodata.*)
    }

    .data : {
        *(.sdata)
        *(.sdata.*)
        *(.data)
        *(.data.*)
    }

    .bss : {
        __bss_start = .;

        *(.sbss)
        *(.sbss.*)
        *(.bss)
        *(.bss.*)
        *(COMMON)

        __bss_stop = .;
    }

    . = ALIGN(16);

    __stack_bottom = .;
    . += 0x4000;
    __stack_top = .;

    . = ALIGN(4096);

    __kernel_end = .;
    _end = .;
}