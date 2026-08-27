#ifndef FDT_H
#define FDT_H

int fdt_path_offset(const void* fdt, const char* path);
const void* fdt_getprop(const void* fdt, int nodeoffset, const char* name, int* lenp);

unsigned long dtb_get_reg_base(const void *fdt, const char *path);
unsigned long get_uart_base_from_dtb(const void *fdt);
unsigned long dtb_get_u64_prop(const void *fdt, const char *path, const char *prop_name);

unsigned long fdt_totalsize(const void *fdt);

int dtb_get_memory_region(const void *fdt, unsigned long *base, unsigned long *size);

#endif