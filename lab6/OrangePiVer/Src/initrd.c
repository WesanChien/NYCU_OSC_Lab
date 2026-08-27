#include "initrd.h"
#include "common.h"
#include "kstring.h"
#include "uart.h"

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

static const void *initrd_start = 0;
static const void *initrd_end = 0;

void initrd_set_range(const void *start, const void *end) {
    initrd_start = start;
    initrd_end = end;
}

int initrd_available(void) {
    return initrd_start &&
           initrd_end &&
           ((unsigned long)initrd_end > (unsigned long)initrd_start);
}

/*
 * 支援 "./filename" 和 "filename" 兩種格式
 */
static int filename_match(const char *archive_name, const char *want) {
    if (str_eq(archive_name, want))
        return 1;

    if (archive_name[0] == '.' &&
        archive_name[1] == '/' &&
        str_eq(archive_name + 2, want)) {
        return 1;
    }

    return 0;
}

void initrd_list(void) {
    if (!initrd_available()) {
        uart_puts("No initrd loaded\n");
        return;
    }

    const char *p = (const char *)initrd_start;

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

        uart_puts(name);
        uart_puts("  size=");
        uart_hex(filesize);
        uart_puts("\n");

        unsigned long off = sizeof(struct cpio_newc_header) + namesize;
        off = align_up_val(off, 4);
        off += filesize;
        off = align_up_val(off, 4);

        p += off;

        if ((unsigned long)p >= (unsigned long)initrd_end)
            break;
    }
}

void initrd_cat(const char* filename) {
    if (!initrd_available()) {
        uart_puts("No initrd loaded\n");
        return;
    }

    const char *p = (const char *)initrd_start;

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

        if ((unsigned long)p >= (unsigned long)initrd_end)
            break;
    }

    uart_puts("File not found: ");
    uart_puts(filename);
    uart_puts("\n");
}

int initrd_find_file(const char *path, const char **data, unsigned long *size) {
    if (!initrd_available()) {
        uart_puts("No initrd loaded\n");
        return -1;
    }

    const char *p = (const char *)initrd_start;

    /*
     * 每個 cpio entry 都至少要有一個 header。
     * 如果剩餘空間連 header 都放不下，就停止, 避免越界。
     */
    while ((unsigned long)p + sizeof(struct cpio_newc_header) <= (unsigned long)initrd_end) {
        const struct cpio_newc_header *hdr = (const struct cpio_newc_header *)p;

        /*
         * newc 格式的 magic 固定是 "070701"。
         * 如果不是，代表 initrd base 錯、資料壞掉，或 p 算錯。
         */
        if (kstrncmp(hdr->c_magic, "070701", 6) != 0) { // 確認目前 p 真的指到一個合法 cpio entry
            uart_puts("Bad cpio magic\n");
            return -1;
        }

        unsigned long namesize = hextoi(hdr->c_namesize, 8);
        unsigned long filesize = hextoi(hdr->c_filesize, 8);

        const char *name = p + sizeof(struct cpio_newc_header); // filename 緊接在 cpio header 後面

        if ((unsigned long)name + namesize > (unsigned long)initrd_end) {
            uart_puts("Bad cpio name range\n");
            return -1;
        }

        if (str_eq(name, "TRAILER!!!"))
            break;

        unsigned long name_end = sizeof(struct cpio_newc_header) + namesize;

        unsigned long data_off = align_up_val(name_end, 4);
        const char *file_data = p + data_off;

        if ((unsigned long)file_data + filesize > (unsigned long)initrd_end) {
            uart_puts("Bad cpio data range\n");
            return -1;
        }

        if (filename_match(name, path)) {
            if (data)
                *data = file_data;

            if (size)
                *size = filesize;

            return 0;
        }

        /*
         * 移到下一個 cpio entry。
         * initrd 是 cpio newc 格式:
         * [cpio header][filename][padding][file data][padding]
         * ...
         * [TRAILER!!!]
         *
         * 下一個 entry 的位置 =
         *     目前 entry 起點 + header + filename area + padding + file data + padding
         */
        unsigned long off = data_off + filesize;
        off = align_up_val(off, 4);
        p += off;
    }

    return -1;
}