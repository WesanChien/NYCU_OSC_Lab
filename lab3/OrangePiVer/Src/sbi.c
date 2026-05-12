#include "sbi.h"
#include "uart.h"

struct sbiret sbi_ecall(int ext,
                        int fid,
                        unsigned long arg0,
                        unsigned long arg1,
                        unsigned long arg2,
                        unsigned long arg3,
                        unsigned long arg4,
                        unsigned long arg5) {
    struct sbiret ret;

    register unsigned long a0 asm("a0") = arg0;
    register unsigned long a1 asm("a1") = arg1;
    register unsigned long a2 asm("a2") = arg2;
    register unsigned long a3 asm("a3") = arg3;
    register unsigned long a4 asm("a4") = arg4;
    register unsigned long a5 asm("a5") = arg5;
    register unsigned long a6 asm("a6") = (unsigned long)fid;
    register unsigned long a7 asm("a7") = (unsigned long)ext;

    asm volatile("ecall"
                 : "+r"(a0), "+r"(a1)
                 : "r"(a2), "r"(a3), "r"(a4), "r"(a5), "r"(a6), "r"(a7)
                 : "memory");

    ret.error = a0;
    ret.value = a1;

    return ret;
}

long sbi_get_spec_version(void) {
    struct sbiret ret = sbi_ecall(
        SBI_EXT_BASE,
        SBI_EXT_BASE_GET_SPEC_VERSION,
        0, 0, 0, 0, 0, 0
    );

    if (ret.error != 0)
        return 0;

    return ret.value;
}

long sbi_get_impl_id(void) {
    struct sbiret ret = sbi_ecall(
        SBI_EXT_BASE,
        SBI_EXT_BASE_GET_IMP_ID,
        0, 0, 0, 0, 0, 0
    );

    if (ret.error != 0)
        return 0;

    return ret.value;
}

long sbi_get_impl_version(void) {
    struct sbiret ret = sbi_ecall(
        SBI_EXT_BASE,
        SBI_EXT_BASE_GET_IMP_VERSION,
        0, 0, 0, 0, 0, 0
    );

    if (ret.error != 0)
        return 0;

    return ret.value;
}

long sbi_probe_extension(int extid) {
    struct sbiret ret = sbi_ecall(
        SBI_EXT_BASE,
        SBI_EXT_BASE_PROBE_EXT,
        extid, 0, 0, 0, 0, 0
    );

    if (ret.error != 0)
        return 0;

    return ret.value;
}

void print_info(void) {
    uart_puts("OpenSBI specification version: ");
    uart_hex(sbi_get_spec_version());
    uart_puts("\n");

    uart_puts("Implementation ID: ");
    uart_hex(sbi_get_impl_id());
    uart_puts("\n");

    uart_puts("Implementation version: ");
    uart_hex(sbi_get_impl_version());
    uart_puts("\n");
}