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