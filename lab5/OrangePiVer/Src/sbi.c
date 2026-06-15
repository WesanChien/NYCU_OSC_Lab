#include "sbi.h"
#include "uart.h"

#define SBI_EXT_TIME              0x54494D45
#define SBI_EXT_TIME_SET_TIMER    0
#define SBI_LEGACY_SET_TIMER      0x00

struct sbiret sbi_ecall(long ext,
                        long fid,
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

static int use_legacy_timer = 0;

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

long sbi_probe_extension(long extid) {
    struct sbiret ret = sbi_ecall(
        SBI_EXT_BASE,
        SBI_EXT_BASE_PROBE_EXT,
        extid, 0, 0, 0, 0, 0
    );

    if (ret.error != 0)
        return 0;

    return ret.value;
}

void sbi_timer_init(void) {
    if (sbi_probe_extension(SBI_EXT_TIME)) {
        use_legacy_timer = 0;
        uart_puts("[sbi] using modern TIME extension\n");
    } else {
        use_legacy_timer = 1;
        uart_puts("[sbi] using legacy set_timer\n");
    }
}

static long sbi_set_timer_legacy(unsigned long stime_value) {
    /*
     * Legacy SBI set_timer:
     *   a7 = 0x00
     *   a0 = target time value
     *
     * 這和 modern SBI v0.2+ 的呼叫方式不同。
     * 成功 repo 也是用這種 direct assembly 方式呼叫 legacy set_timer。
     */
    register unsigned long a0 asm("a0") = stime_value;
    register unsigned long a7 asm("a7") = SBI_EXT_LEGACY_SET_TIMER;

    asm volatile("ecall"
                 : "+r"(a0)
                 : "r"(a7)
                 : "memory");

    return (long)a0;
}

long sbi_set_timer(unsigned long stime_value) {
    static int timer_mode = -1;
    /*
     * timer_mode:
     *   -1 = 尚未判斷
     *    0 = legacy SBI set_timer
     *    1 = modern SBI TIME extension
     */

    if (timer_mode < 0) {
        long probe = sbi_probe_extension(SBI_EXT_TIME);

        uart_puts("[SBI] probe TIME: ");
        uart_hex(probe);
        uart_puts("\n");

        if (probe > 0) {
            timer_mode = 1;
            uart_puts("[SBI] timer mode: TIME\n");
        } else {
            timer_mode = 0;
            uart_puts("[SBI] timer mode: LEGACY\n");
        }
    }

    if (timer_mode == 1) {
        struct sbiret ret = sbi_ecall(SBI_EXT_TIME,
                                      SBI_EXT_TIME_SET_TIMER,
                                      stime_value,
                                      0, 0, 0, 0, 0);

        /*
         * 目前 modern TIME 回 -2，
         * 所以遇到 -2 就切到 legacy
         */
        if (ret.error == -2) {
            uart_puts("[SBI] TIME unsupported, switch to LEGACY\n");
            timer_mode = 0;
            return sbi_set_timer_legacy(stime_value);
        }

        return ret.error;
    }

    return sbi_set_timer_legacy(stime_value);
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