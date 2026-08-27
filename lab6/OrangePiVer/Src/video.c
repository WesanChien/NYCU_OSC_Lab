#include "video.h"
#include "uart.h"

#define FB_BASE         0x7f700000UL

#define FB_WIDTH        1920
#define FB_HEIGHT       1080

#define CACHE_BLOCK_SIZE 64

static unsigned long align_down(unsigned long x, unsigned long align) {
    return x & ~(align - 1);
}

static unsigned long align_up(unsigned long x, unsigned long align) {
    return (x + align - 1) & ~(align - 1);
}

/*
 * 如果 toolchain 不支援 -march=rv64gc_zicbom，
 * 用 raw instruction encoding。
 */
static inline void cbo_flush_one(unsigned long addr) {
    asm volatile(
        "mv a0, %0\n\t"
        ".word 0x0025200F\n\t"
        :
        : "r"(addr)
        : "memory", "a0"
    );
}

static void flush_dcache(void *addr, unsigned long len) {
    unsigned long start = align_down((unsigned long)addr, CACHE_BLOCK_SIZE);
    unsigned long end = align_up((unsigned long)addr + len, CACHE_BLOCK_SIZE);

    for (unsigned long p = start; p < end; p += CACHE_BLOCK_SIZE)
        cbo_flush_one(p);

    asm volatile("fence rw, rw" ::: "memory");
}

static void copy_words(unsigned int *dst, const unsigned int *src, unsigned long count) {
    for (unsigned long i = 0; i < count; i++)
        dst[i] = src[i];
}

int video_display(unsigned int *bmp_image, unsigned int width, unsigned int height) {
    if (bmp_image == 0)
        return -1;

    if (width == 0 || height == 0)
        return -1;

    if (width > FB_WIDTH || height > FB_HEIGHT)
        return -1;

    unsigned int *fb = (unsigned int *)FB_BASE;

    /* 計算圖片在 framebuffer 中的起始(置中)位置*/
    unsigned int start_x = (FB_WIDTH - width) / 2;
    unsigned int start_y = (FB_HEIGHT - height) / 2;

    /* 把 bmp_image 的每一列 copy 到 framebuffer，並 flush cache */
    for (unsigned int y = 0; y < height; y++) {
        unsigned int *dst = fb + (start_y + y) * FB_WIDTH + start_x;
        unsigned int *src = bmp_image + y * width;

        copy_words(dst, src, width);

        /*
         * 每列 flush 一次，避免 display controller 讀到舊 cache。
         */
        flush_dcache(dst, width * sizeof(unsigned int));
    }

    return 0;
}