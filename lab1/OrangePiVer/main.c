extern char uart_getc(void);
extern void uart_putc(char c);
extern void uart_puts(const char* s);
extern void uart_hex(unsigned long h);

#define SBI_EXT_SET_TIMER 0x0
#define SBI_EXT_SHUTDOWN  0x8
#define SBI_EXT_BASE      0x10

enum sbi_ext_base_fid {
    SBI_EXT_BASE_GET_SPEC_VERSION,
    SBI_EXT_BASE_GET_IMP_ID,
    SBI_EXT_BASE_GET_IMP_VERSION,
    SBI_EXT_BASE_PROBE_EXT,
    SBI_EXT_BASE_GET_MVENDORID,
    SBI_EXT_BASE_GET_MARCHID,
    SBI_EXT_BASE_GET_MIMPID,
};

struct sbiret {
    long error;
    long value;
};

struct sbiret sbi_ecall(int ext,
                        int fid,
                        unsigned long arg0,
                        unsigned long arg1,
                        unsigned long arg2,
                        unsigned long arg3,
                        unsigned long arg4,
                        unsigned long arg5) {
    struct sbiret ret;
    register unsigned long a0 asm("a0") = (unsigned long)arg0;
    register unsigned long a1 asm("a1") = (unsigned long)arg1;
    register unsigned long a2 asm("a2") = (unsigned long)arg2;
    register unsigned long a3 asm("a3") = (unsigned long)arg3;
    register unsigned long a4 asm("a4") = (unsigned long)arg4;
    register unsigned long a5 asm("a5") = (unsigned long)arg5;
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

/**
 * sbi_get_spec_version() - Get the SBI specification version.
 *
 * Return: The current SBI specification version.
 * The minor number of the SBI specification is encoded in the low 24 bits,
 * with the major number encoded in the next 7 bits. Bit 31 must be 0.
 */
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

/**
 * sbi_probe_extension() - Check if an SBI extension ID is supported or not.
 * @extid: The extension ID to be probed.
 *
 * Return: 1 or an extension specific nonzero value if yes, 0 otherwise.
 */
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

int str_eq(const char *a, const char *b) {
    while (*a && *b) {
        if (*a != *b)
            return 0;
        a++;
        b++;
    }
    return (*a == '\0' && *b == '\0');
}

void print_help(void) {
    uart_puts("Available commands:\n");
    uart_puts("  help  - show this help message\n");
    uart_puts("  hello - print Hello World!\n");
    uart_puts("  info  - show OpenSBI information\n");
}

void start_kernel() {
    char buf[64];
    int idx;

    uart_puts("\nStarting OSC Lab1 kernel ...\n");
    uart_puts("Type 'help' for commands.\n");

    while (1) {
        uart_puts("OrangePi RV2> ");
        idx = 0;

        while (1) {
            char c = uart_getc();

            if (c == '\n') {
                uart_putc('\n');
                buf[idx] = '\0';
                break;
            }

            if (c == '\b' || c == 127) {
                if (idx > 0) {
                    idx--;
                    uart_puts("\b \b");
                }
                continue;
            }

            if (idx < (int)sizeof(buf) - 1) {
                buf[idx++] = c;
                uart_putc(c);
            }
        }

        if (str_eq(buf, "help")) {
            print_help();
        } else if (str_eq(buf, "hello")) {
            uart_puts("Hello World!\n");
        } else if (str_eq(buf, "info")) {
            print_info();
        } else if (buf[0] != '\0') {
            uart_puts("Unknown command: ");
            uart_puts(buf);
            uart_puts("\n");
        }
    }
}
