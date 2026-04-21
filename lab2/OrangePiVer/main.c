extern char uart_getc(void);
extern void uart_putc(char c);
extern void uart_puts(const char* s);
extern void uart_hex(unsigned long h);
extern void uart_init(unsigned long base);

#define SBI_EXT_SET_TIMER 0x0
#define SBI_EXT_SHUTDOWN  0x8
#define SBI_EXT_BASE      0x10

#define FDT_BEGIN_NODE 0x00000001
#define FDT_END_NODE   0x00000002
#define FDT_PROP       0x00000003
#define FDT_NOP        0x00000004
#define FDT_END        0x00000009

#define LOAD_ADDR 0x20000000UL // To avoid overwriting itself, the bootloader should load the new kernel into a different memory address
#define BOOT_MAGIC 0x544F4F42UL // 這個數字對應 ASCII "BOOT"

/* Read a 32-bit little-endian word from UART.
 * Host side sends the boot header in little-endian format.
 */
static unsigned int uart_get_u32_le(void) {
    unsigned int b0 = (unsigned char)uart_getc();
    unsigned int b1 = (unsigned char)uart_getc();
    unsigned int b2 = (unsigned char)uart_getc();
    unsigned int b3 = (unsigned char)uart_getc();
    return b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
}

typedef void (*kernel_entry_t)(unsigned long);

static void uart_put_u32_hex(unsigned int x) {
    uart_hex((unsigned long)x);
}

/* Receive a raw kernel image over UART and transfer control to it.
 *
 * Protocol:[4 bytes magic][4 bytes size][raw binary payload]
 * 
 * The payload must already be linked for execution at LOAD_ADDR.
 */
void load_image_and_boot(unsigned long fdt_addr) {
    unsigned int magic, size;
    unsigned char *dst = (unsigned char *)LOAD_ADDR; // dst 指向 0x20000000

    uart_puts("Waiting for image header...\n");

    magic = uart_get_u32_le();
    size  = uart_get_u32_le();

    if (magic != BOOT_MAGIC) {
        uart_puts("Bad magic: ");
        uart_put_u32_hex(magic);
        uart_puts("\n");
        return;
    }

    uart_puts("Image size: ");
    uart_put_u32_hex(size);
    uart_puts("\n");

    for (unsigned int i = 0; i < size; i++) {
        dst[i] = (unsigned char)uart_getc(); // 每收一個 byte，就直接寫進 RAM, 最後整份 loader_target.bin 會完整出現在那裡
    }

    uart_puts("Image loaded at ");
    uart_hex(LOAD_ADDR);
    uart_puts("\nJumping...\n");

    /* Hand over control to the loaded image.
     * We restore the DTB pointer in a1 to follow the normal RISC-V boot convention,
     * then jump directly to the image entry at LOAD_ADDR.
     */
    asm volatile( 
        "mv a1, %0\n" // mv a1, fdt_addr
        "jr %1\n"     // jr LOAD_ADDR
        :
        : "r"(fdt_addr), "r"(LOAD_ADDR)
        : "a1", "memory"
    );
}

/* Flattened Device Tree (FDT) header.
 * off_dt_struct points to the structure block,
 * off_dt_strings points to the strings block.
 */
struct fdt_header {
    unsigned int magic;
    unsigned int totalsize;
    unsigned int off_dt_struct;
    unsigned int off_dt_strings;
    unsigned int off_mem_rsvmap;
    unsigned int version;
    unsigned int last_comp_version;
    unsigned int boot_cpuid_phys;
    unsigned int size_dt_strings;
    unsigned int size_dt_struct;
};

struct cpio_newc_header {
    char c_magic[6];
    char c_ino[8];
    char c_mode[8];
    char c_uid[8];
    char c_gid[8];
    char c_nlink[8];
    char c_mtime[8];
    char c_filesize[8];
    char c_devmajor[8];
    char c_devminor[8];
    char c_rdevmajor[8];
    char c_rdevminor[8];
    char c_namesize[8];
    char c_check[8];
};

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

static const void *initrd_start = 0;
static const void *initrd_end = 0;

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

static inline const void *align_up_ptr(const void *ptr, unsigned long align) {
    unsigned long p = (unsigned long)ptr;
    return (const void *)((p + align - 1) & ~(align - 1));
}

static inline unsigned long align_up_val(unsigned long val, unsigned long align) {
    return (val + align - 1) & ~(align - 1);
}

unsigned long kstrlen(const char *s) {
    unsigned long n = 0;
    while (s[n]) n++;
    return n;
}

int kstrcmp(const char *a, const char *b) {
    while (*a && *b) {
        if (*a != *b)
            return (unsigned char)*a - (unsigned char)*b;
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

int kstrncmp(const char *a, const char *b, unsigned long n) {
    while (n > 0 && *a && *b) {
        if (*a != *b)
            return (unsigned char)*a - (unsigned char)*b;
        a++;
        b++;
        n--;
    }
    if (n == 0)
        return 0;
    return (unsigned char)*a - (unsigned char)*b;
}

int str_eq(const char *a, const char *b) {
    return kstrcmp(a, b) == 0;
}

int starts_with(const char *s, const char *prefix) {
    while (*prefix) {
        if (*s != *prefix)
            return 0;
        s++;
        prefix++;
    }
    return 1;
}

unsigned long hextoi(const char *s, int n) {
    unsigned long r = 0;
    while (n-- > 0) {
        r <<= 4;
        if (*s >= '0' && *s <= '9') r += *s - '0';
        else if (*s >= 'a' && *s <= 'f') r += *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'F') r += *s - 'A' + 10;
        s++;
    }
    return r;
}

/* Match a path segment against a DT node name.
 * Accept both exact matches ("memory") and unit-address form
 * such as "memory@80000000".
 */
static int node_name_match(const char *node_name, const char *path_seg, int seg_len) {
    if (kstrncmp(node_name, path_seg, seg_len) != 0)
        return 0;

    if (node_name[seg_len] == '\0')
        return 1;

    if (node_name[seg_len] == '@')
        return 1;

    return 0;
}

/* FDT stores integer fields in big-endian format, 
 * so we must byte-swap them before using them on our little-endian execution environment.
 */
static inline unsigned int bswap32(unsigned int x) {
    return ((x & 0x000000ffU) << 24) |
           ((x & 0x0000ff00U) << 8)  |
           ((x & 0x00ff0000U) >> 8)  |
           ((x & 0xff000000U) >> 24);
}

static inline unsigned long bswap64(unsigned long x) {
    return ((x & 0x00000000000000ffUL) << 56) |
           ((x & 0x000000000000ff00UL) << 40) |
           ((x & 0x0000000000ff0000UL) << 24) |
           ((x & 0x00000000ff000000UL) << 8)  |
           ((x & 0x000000ff00000000UL) >> 8)  |
           ((x & 0x0000ff0000000000UL) >> 24) |
           ((x & 0x00ff000000000000UL) >> 40) |
           ((x & 0xff00000000000000UL) >> 56);
}

/* 在裝置樹中找節點
 * Find the structure-block offset of the first node matching the given path.
 * Example path: "/soc/serial" or "/chosen".
 */
int fdt_path_offset(const void* fdt, const char* path) {
    const struct fdt_header *hdr = (const struct fdt_header *)fdt;
    if (bswap32(hdr->magic) != 0xd00dfeed)
        return -1;

    const char *struct_blk = (const char *)fdt + bswap32(hdr->off_dt_struct);
    const char *p = struct_blk;

    int depth = 0;
    int matched_depth = 0;

    while (1) {
        const char *token_pos = p;
        unsigned int token = bswap32(*(const unsigned int *)p);
        p += 4;

        if (token == FDT_BEGIN_NODE) {
            const char *name = p;

            if (depth == 0) {
                matched_depth = 0;
            } else if (path[0] == '/') {
                const char *seg = path + 1;
                int cur_depth = depth - 1;

                for (int i = 0; i < cur_depth; i++) {
                    while (*seg && *seg != '/') seg++;
                    if (*seg == '/') seg++;
                }

                if (*seg) {
                    const char *seg_end = seg;
                    while (*seg_end && *seg_end != '/') seg_end++;
                    int seg_len = (int)(seg_end - seg);

                    if (cur_depth == matched_depth &&
                        node_name_match(name, seg, seg_len)) {
                        matched_depth++;
                    }
                }
            }

            p = (const char *)align_up_ptr(name + kstrlen(name) + 1, 4);

            if (path[0] == '/') {
                const char *rest = path + 1;
                int count = 0;
                while (*rest) {
                    count++;
                    while (*rest && *rest != '/') rest++;
                    if (*rest == '/') rest++;
                }

                if (matched_depth == count)
                    return (int)(token_pos - struct_blk);
            }

            depth++;
        } else if (token == FDT_END_NODE) {
            depth--;
            if (matched_depth > depth - 1)
                matched_depth = depth - 1;
            if (matched_depth < 0)
                matched_depth = 0;
        } else if (token == FDT_PROP) {
            unsigned int len = bswap32(*(const unsigned int *)p);
            p += 4;
            p += 4;
            p = (const char *)align_up_ptr(p + len, 4);
        } else if (token == FDT_NOP) {
            continue;
        } else if (token == FDT_END) {
            break;
        } else {
            return -1;
        }
    }

    return -1;
}

/* 在節點中找欄位
 * Look up a property inside a node identified by nodeoffset from fdt_path_offset().
 * Returns a pointer to the property value and writes its length to lenp.
 */
const void* fdt_getprop(const void* fdt, int nodeoffset, const char* name, int* lenp) {
    const struct fdt_header *hdr = (const struct fdt_header *)fdt;
    if (bswap32(hdr->magic) != 0xd00dfeed)
        return 0;

    const char *struct_blk = (const char *)fdt + bswap32(hdr->off_dt_struct);
    const char *strings_blk = (const char *)fdt + bswap32(hdr->off_dt_strings);
    const char *p = struct_blk + nodeoffset;

    unsigned int token = bswap32(*(const unsigned int *)p);
    if (token != FDT_BEGIN_NODE)
        return 0;

    p += 4;
    p = (const char *)align_up_ptr(p + kstrlen(p) + 1, 4);

    int depth = 0;

    while (1) {
        token = bswap32(*(const unsigned int *)p);
        p += 4;

        if (token == FDT_PROP) {
            unsigned int len = bswap32(*(const unsigned int *)p);
            p += 4;
            unsigned int nameoff = bswap32(*(const unsigned int *)p);
            p += 4;

            const char *prop_name = strings_blk + nameoff;
            const void *prop_data = p;

            if (kstrcmp(prop_name, name) == 0) {
                if (lenp) *lenp = (int)len;
                return prop_data;
            }

            p = (const char *)align_up_ptr(p + len, 4);
        } else if (token == FDT_BEGIN_NODE) {
            depth++;
            p = (const char *)align_up_ptr(p + kstrlen(p) + 1, 4);
        } else if (token == FDT_END_NODE) {
            if (depth == 0)
                break;
            depth--;
        } else if (token == FDT_NOP) {
            continue;
        } else if (token == FDT_END) {
            break;
        } else {
            return 0;
        }
    }

    return 0;
}

unsigned long dtb_get_reg_base(const void *fdt, const char *path) {
    int len = 0;
    int off = fdt_path_offset(fdt, path);
    if (off < 0)
        return 0;

    const void *prop = fdt_getprop(fdt, off, "reg", &len);
    if (!prop)
        return 0;

    if (len >= 8) {
        return bswap64(*(const unsigned long *)prop);
    }

    return 0;
}

unsigned long get_uart_base_from_dtb(const void *fdt) {
    unsigned long base = dtb_get_reg_base(fdt, "/soc/serial");
    if (base)
        return base;

    base = dtb_get_reg_base(fdt, "/soc/uart");
    return base;
}

unsigned long dtb_get_u64_prop(const void *fdt, const char *path, const char *prop_name) {
    int len = 0;
    int off = fdt_path_offset(fdt, path);
    if (off < 0)
        return 0;

    const void *prop = fdt_getprop(fdt, off, prop_name, &len);
    if (!prop || len < 8)
        return 0;

    return bswap64(*(const unsigned long *)prop);
}

int filename_match(const char *archive_name, const char *want) {
    if (str_eq(archive_name, want))
        return 1;

    if (archive_name[0] == '.' && archive_name[1] == '/' && str_eq(archive_name + 2, want))
        return 1;

    return 0;
}

/* 從 archive 開頭一路掃到尾，把每個檔案的名字與大小印出來。
 */
void initrd_list(const void* rd) {
    const char *p = (const char *)rd; // p 指向 archive 起點

    while (1) {
        const struct cpio_newc_header *hdr = (const struct cpio_newc_header *)p; // 把 p cast 成 struct cpio_newc_header *

        if (kstrncmp(hdr->c_magic, "070701", 6) != 0) {
            uart_puts("Bad cpio magic\n");
            return;
        }

        unsigned long namesize = hextoi(hdr->c_namesize, 8);
        unsigned long filesize = hextoi(hdr->c_filesize, 8);
        const char *name = p + sizeof(struct cpio_newc_header);

        if (str_eq(name, "TRAILER!!!"))
            break;

        uart_puts(name);
        uart_puts("  size=");
        uart_hex(filesize);
        uart_puts("\n");

        unsigned long off = sizeof(struct cpio_newc_header) + namesize;
        off = align_up_val(off, 4);
        off += filesize;
        off = align_up_val(off, 4);
        p += off;

        if ((const void *)p >= initrd_end)
            break;
    }
}

/* Print the content of a file stored in the initramfs cpio archive.
 */
void initrd_cat(const void* rd, const char* filename) {
    const char *p = (const char *)rd;

    while (1) {
        const struct cpio_newc_header *hdr = (const struct cpio_newc_header *)p;

        if (kstrncmp(hdr->c_magic, "070701", 6) != 0) {
            uart_puts("Bad cpio magic\n");
            return;
        }

        unsigned long namesize = hextoi(hdr->c_namesize, 8);
        unsigned long filesize = hextoi(hdr->c_filesize, 8);
        const char *name = p + sizeof(struct cpio_newc_header);

        if (str_eq(name, "TRAILER!!!"))
            break;

        unsigned long name_end = sizeof(struct cpio_newc_header) + namesize;
        unsigned long data_off = align_up_val(name_end, 4);
        const char *data = p + data_off;

        if (filename_match(name, filename)) {
            for (unsigned long i = 0; i < filesize; i++)
                uart_putc(data[i]);
            uart_puts("\n");
            return;
        }

        unsigned long off = data_off + filesize;
        off = align_up_val(off, 4);
        p += off;

        if ((const void *)p >= initrd_end)
            break;
    }

    uart_puts("File not found: ");
    uart_puts(filename);
    uart_puts("\n");
}

const char *skip_spaces(const char *s) {
    while (*s == ' ')
        s++;
    return s;
}

void print_help(void) {
    uart_puts("Available commands:\n");
    uart_puts("  help  - show this help message\n");
    uart_puts("  hello - print Hello World!\n");
    uart_puts("  info  - show OpenSBI information\n");
    uart_puts("  ls    - list files in initramfs\n");
    uart_puts("  cat <file>  - show file content from initramfs\n");
    uart_puts("  load        - load a kernel image over UART and boot it\n");
}

void start_kernel(unsigned long fdt_addr) {
    char buf[64];
    int idx;

    const void *fdt = (const void *)fdt_addr;

    unsigned long uart_base = get_uart_base_from_dtb(fdt);
    if (!uart_base) {
        while (1) { }
    }
    uart_init(uart_base);

    initrd_start = (const void *)dtb_get_u64_prop(fdt, "/chosen", "linux,initrd-start");
    initrd_end   = (const void *)dtb_get_u64_prop(fdt, "/chosen", "linux,initrd-end");

    uart_puts("\nStarting OSC loaded Lab2 kernel ...\n");
    uart_puts("UART base from DTB: ");
    uart_hex(uart_base);
    uart_puts("\n");

    uart_puts("initrd-start: ");
    uart_hex((unsigned long)initrd_start);
    uart_puts("\n");

    uart_puts("initrd-end:   ");
    uart_hex((unsigned long)initrd_end);
    uart_puts("\n");

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
        } else if (str_eq(buf, "ls")) {
            if (initrd_start && initrd_end && initrd_end > initrd_start)
                initrd_list(initrd_start);
            else
                uart_puts("No initrd loaded\n");
        } else if (starts_with(buf, "cat")) {
            const char *name = skip_spaces(buf + 3);
            if (*name == '\0') {
                uart_puts("Usage: cat <file>\n");
            } else if (initrd_start && initrd_end && initrd_end > initrd_start) {
                initrd_cat(initrd_start, name);
            } else {
                uart_puts("No initrd loaded\n");
            }
        } else if (str_eq(buf, "load")) {
            load_image_and_boot(fdt_addr);
        } else if (buf[0] != '\0') {
            uart_puts("Unknown command: ");
            uart_puts(buf);
            uart_puts("\n");
        } 
    }
}