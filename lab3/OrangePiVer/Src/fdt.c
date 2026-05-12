#include "fdt.h"
#include "common.h"
#include "kstring.h"

#define FDT_BEGIN_NODE 0x00000001
#define FDT_END_NODE   0x00000002
#define FDT_PROP       0x00000003
#define FDT_NOP        0x00000004
#define FDT_END        0x00000009

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

static unsigned int fdt32(const void *ptr) {
    const unsigned char *p = (const unsigned char *)ptr;

    return ((unsigned int)p[0] << 24) |
           ((unsigned int)p[1] << 16) |
           ((unsigned int)p[2] << 8)  |
           ((unsigned int)p[3]);
}

static unsigned long fdt64(const void *ptr) {
    const unsigned char *p = (const unsigned char *)ptr;

    return ((unsigned long)p[0] << 56) |
           ((unsigned long)p[1] << 48) |
           ((unsigned long)p[2] << 40) |
           ((unsigned long)p[3] << 32) |
           ((unsigned long)p[4] << 24) |
           ((unsigned long)p[5] << 16) |
           ((unsigned long)p[6] << 8)  |
           ((unsigned long)p[7]);
}

static int node_name_match(const char *node_name, const char *path_seg, int seg_len) {
    if (kstrncmp(node_name, path_seg, seg_len) != 0)
        return 0;

    if (node_name[seg_len] == '\0')
        return 1;

    if (node_name[seg_len] == '@')
        return 1;

    return 0;
}

int fdt_path_offset(const void* fdt, const char* path) {
    const struct fdt_header *hdr = (const struct fdt_header *)fdt;

    if (fdt32(&hdr->magic) != 0xd00dfeed)
        return -1;

    const char *struct_blk = (const char *)fdt + fdt32(&hdr->off_dt_struct);
    const char *p = struct_blk;

    int depth = 0;
    int matched_depth = 0;

    while (1) {
        const char *token_pos = p;
        unsigned int token = fdt32(p);
        p += 4;

        if (token == FDT_BEGIN_NODE) {
            const char *name = p;

            if (depth == 0) {
                matched_depth = 0;
            } else if (path[0] == '/') {
                const char *seg = path + 1;
                int cur_depth = depth - 1;

                for (int i = 0; i < cur_depth; i++) {
                    while (*seg && *seg != '/')
                        seg++;

                    if (*seg == '/')
                        seg++;
                }

                if (*seg) {
                    const char *seg_end = seg;

                    while (*seg_end && *seg_end != '/')
                        seg_end++;

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

                    while (*rest && *rest != '/')
                        rest++;

                    if (*rest == '/')
                        rest++;
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
            unsigned int len = fdt32(p);
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

const void* fdt_getprop(const void* fdt, int nodeoffset, const char* name, int* lenp) {
    const struct fdt_header *hdr = (const struct fdt_header *)fdt;

    if (fdt32(&hdr->magic) != 0xd00dfeed)
        return 0;

    const char *struct_blk = (const char *)fdt + fdt32(&hdr->off_dt_struct);
    const char *strings_blk = (const char *)fdt + fdt32(&hdr->off_dt_strings);
    const char *p = struct_blk + nodeoffset;

    unsigned int token = fdt32(p);

    if (token != FDT_BEGIN_NODE)
        return 0;

    p += 4;
    p = (const char *)align_up_ptr(p + kstrlen(p) + 1, 4);

    int depth = 0;

    while (1) {
        token = fdt32(p);
        p += 4;

        if (token == FDT_PROP) {
            unsigned int len = fdt32(p);
            p += 4;

            unsigned int nameoff = fdt32(p);
            p += 4;

            const char *prop_name = strings_blk + nameoff;
            const void *prop_data = p;

            if (kstrcmp(prop_name, name) == 0) {
                if (lenp)
                    *lenp = (int)len;

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

    if (len >= 8)
        return fdt64(prop);

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

    return fdt64(prop);
}

unsigned long fdt_totalsize(const void *fdt) {
    const struct fdt_header *hdr = (const struct fdt_header *)fdt;

    if (fdt32(&hdr->magic) != 0xd00dfeed)
        return 0;

    return fdt32(&hdr->totalsize);
}

int dtb_get_memory_region(const void *fdt, unsigned long *base, unsigned long *size) {
    int len = 0;

    int off = fdt_path_offset(fdt, "/memory");

    if (off < 0)
        return 0;

    const void *prop = fdt_getprop(fdt, off, "reg", &len);

    if (!prop)
        return 0;

    /*
     * OrangePi RV2 的 /memory reg 通常是：
     *
     *   <base_hi base_lo size_hi size_lo>
     *
     * 也就是 2 address cells + 2 size cells。
     * 總共 16 bytes。
     *
     * 第一個 64-bit 是 base。
     * 第二個 64-bit 是 size。
     */
    if (len >= 16) {
        *base = fdt64(prop);
        *size = fdt64((const char *)prop + 8);
        return 1;
    }

    /*
     * 保留一個 fallback。
     * 某些 DTB 可能用 32-bit address cell / size cell。
     */
    if (len >= 8) {
        *base = fdt32(prop);
        *size = fdt32((const char *)prop + 4);
        return 1;
    }

    return 0;
}