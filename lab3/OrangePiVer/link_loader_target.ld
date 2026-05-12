OUTPUT_ARCH(riscv)
ENTRY(_start)
SECTIONS
{
    . = 0x20000000;
    __kernel_start = .;
    .text : { *(.text.boot) *(.text) }
    .rodata : { *(.rodata) }
    .data : { *(.data) }
    .bss : {
        __bss_start = .;
        *(.sbss) *(.bss)
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