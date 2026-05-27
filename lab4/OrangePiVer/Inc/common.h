#ifndef COMMON_H
#define COMMON_H

static inline const void *align_up_ptr(const void *ptr, unsigned long align) {
    unsigned long p = (unsigned long)ptr;
    return (const void *)((p + align - 1) & ~(align - 1));
}

static inline unsigned long align_up_val(unsigned long val, unsigned long align) {
    return (val + align - 1) & ~(align - 1);
}

#endif