#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FDT_BEGIN_NODE 0x00000001
#define FDT_END_NODE   0x00000002
#define FDT_PROP       0x00000003
#define FDT_NOP        0x00000004
#define FDT_END        0x00000009

struct fdt_header {
    uint32_t magic; // legal dtb magic: 0xd00dfeed
    uint32_t totalsize;
    uint32_t off_dt_struct;
    uint32_t off_dt_strings;
    uint32_t off_mem_rsvmap;
    uint32_t version;
    uint32_t last_comp_version;
    uint32_t boot_cpuid_phys;
    uint32_t size_dt_strings;
    uint32_t size_dt_struct;
};

static inline uint32_t bswap32(uint32_t x) { // Endian conversion for 32-bit integers
    return __builtin_bswap32(x);
}

static inline uint64_t bswap64(uint64_t x) { // Endian conversion for 64-bit integers
    return __builtin_bswap64(x);
}

static inline const void* align_up(const void* ptr, size_t align) { //node name 和 property data 都是 4-byte aligned
    return (const void*)(((uintptr_t)ptr + align - 1) & ~(align - 1));
}

static void build_path(char *out, char comps[][128], int depth) {
    if (depth == 0) {
        strcpy(out, "/");
        return;
    }

    out[0] = '\0';
    for (int i = 0; i < depth; i++) {
        strcat(out, "/");
        strcat(out, comps[i]);
    }
}

static int node_name_match(const char *node_name, const char *path_seg, int seg_len) {
    if (strncmp(node_name, path_seg, seg_len) != 0)
        return 0;

    /* 完全相等，例如 "chosen" vs "chosen" */
    if (node_name[seg_len] == '\0')
        return 1;

    /* 允許 "memory" 匹配 "memory@80000000" */
    if (node_name[seg_len] == '@')
        return 1;

    return 0;
}

int fdt_path_offset(const void* fdt, const char* path) {
    const struct fdt_header *hdr = (const struct fdt_header *)fdt;
    if (bswap32(hdr->magic) != 0xd00dfeed) {
        return -1;
    }

    const char *struct_blk = (const char *)fdt + bswap32(hdr->off_dt_struct);
    const char *p = struct_blk;

    int depth = 0;
    int matched_depth = 0;

    while (1) {
        const char *token_pos = p;
        uint32_t token = bswap32(*(const uint32_t *)p);
        p += 4;

        if (token == FDT_BEGIN_NODE) {
            const char *name = p;

            if (depth == 0) {
                /* root node，通常名稱是空字串 */
                matched_depth = 0;
            } else {
                if (path[0] == '/') {
                    const char *seg = path + 1;
                    int cur_depth = depth - 1;

                    for (int i = 0; i < cur_depth; i++) {
                        while (*seg && *seg != '/') seg++;
                        if (*seg == '/') seg++;
                    }

                    if (*seg) {
                        const char *seg_end = seg;
                        while (*seg_end && *seg_end != '/') seg_end++;
                        int seg_len = seg_end - seg;

                        if (cur_depth == matched_depth &&
                            node_name_match(name, seg, seg_len)) {
                            matched_depth++;
                        }
                    }
                }
            }

            p = align_up(name + strlen(name) + 1, 4);

            /* 檢查是否已完整匹配 */
            if (path[0] == '/') {
                const char *rest = path + 1;
                int count = 0;
                while (*rest) {
                    count++;
                    while (*rest && *rest != '/') rest++;
                    if (*rest == '/') rest++;
                }

                if (matched_depth == count) {
                    return (int)(token_pos - struct_blk);
                }
            }

            depth++;
        } else if (token == FDT_END_NODE) {
            depth--;
            if (matched_depth > depth - 1)
                matched_depth = depth - 1;
            if (matched_depth < 0)
                matched_depth = 0;
        } else if (token == FDT_PROP) {
            uint32_t len = bswap32(*(const uint32_t *)p);
            p += 4;
            p += 4; /* skip nameoff */
            p = align_up(p + len, 4);
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
    if (bswap32(hdr->magic) != 0xd00dfeed) {
        return NULL;
    }

    const char *struct_blk = (const char *)fdt + bswap32(hdr->off_dt_struct);
    const char *strings_blk = (const char *)fdt + bswap32(hdr->off_dt_strings);
    const char *p = struct_blk + nodeoffset;

    uint32_t token = bswap32(*(const uint32_t *)p);
    if (token != FDT_BEGIN_NODE) {
        return NULL;
    }

    p += 4;
    p = align_up(p + strlen(p) + 1, 4);

    int depth = 0;

    while (1) {
        token = bswap32(*(const uint32_t *)p);
        p += 4;

        if (token == FDT_PROP) {
            uint32_t len = bswap32(*(const uint32_t *)p);
            p += 4;
            uint32_t nameoff = bswap32(*(const uint32_t *)p);
            p += 4;

            const char *prop_name = strings_blk + nameoff;
            const void *prop_data = p;

            if (strcmp(prop_name, name) == 0) {
                if (lenp) *lenp = (int)len;
                return prop_data;
            }

            p = align_up(p + len, 4);
        } else if (token == FDT_BEGIN_NODE) {
            depth++;
            p = align_up(p + strlen(p) + 1, 4);
        } else if (token == FDT_END_NODE) {
            if (depth == 0) break;
            depth--;
        } else if (token == FDT_NOP) {
            continue;
        } else if (token == FDT_END) {
            break;
        } else {
            return NULL;
        }
    }

    return NULL;
}

int main() {
    /* Prepare the device tree blob */
    FILE* fp = fopen("qemu.dtb", "rb"); // read the .dtb file
    if (!fp) {
        perror("fopen");
        return EXIT_FAILURE;
    }
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    void* fdt = malloc(sz);
    fseek(fp, 0, SEEK_SET);
    if (fread(fdt, 1, sz, fp) != sz) {
        fprintf(stderr, "Failed to read the device tree blob\n");
        free(fdt);
        fclose(fp);
        return EXIT_FAILURE;
    }
    fclose(fp);

    /* Find the node offset */
    int offset = fdt_path_offset(fdt, "/cpus/cpu@0/interrupt-controller");
    if (offset < 0) {
        fprintf(stderr, "fdt_path_offset\n");
        free(fdt);
        return EXIT_FAILURE;
    }

    /* Get the node property */
    int len;
    const void* prop = fdt_getprop(fdt, offset, "compatible", &len); // compatible: string array
    if (!prop) {
        fprintf(stderr, "fdt_getprop\n");
        free(fdt);
        return EXIT_FAILURE;
    }
    printf("compatible: %.*s\n", len, (const char*)prop);

    /* 找 /memory，取 reg */
    offset = fdt_path_offset(fdt, "/memory");
    prop = fdt_getprop(fdt, offset, "reg", &len); // reg: 64-bit integer array
    const uint64_t* reg = (const uint64_t*)prop;
    printf("memory: base=0x%lx size=0x%lx\n", bswap64(reg[0]), bswap64(reg[1]));

    /* 找 /chosen，取 linux,initrd-start*/
    offset = fdt_path_offset(fdt, "/chosen");
    prop = fdt_getprop(fdt, offset, "linux,initrd-start", &len); // linux,initrd-start: boot-time metadata
    const uint64_t* initrd_start = (const uint64_t*)prop;
    printf("initrd-start: 0x%lx\n", bswap64(initrd_start[0]));

    free(fdt);
    return 0;
}