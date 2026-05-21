extern char uart_getc(void);
extern void uart_putc(char c);
extern void uart_puts(const char* s);
extern void uart_hex(unsigned long h);
extern int hextoi(const char* s, int n);
extern int align(int n, int byte);
extern int memcmp(const void* s1, const void* s2, int n);
extern void* alloc_page();

#define INITRD_BASE 0x88200000
#define STACK_SIZE  0x1000

struct cpio_t {
    char magic[6];
    char ino[8];
    char mode[8];
    char uid[8];
    char gid[8];
    char nlink[8];
    char mtime[8];
    char filesize[8];
    char devmajor[8];
    char devminor[8];
    char rdevmajor[8];
    char rdevminor[8];
    char namesize[8];
    char check[8];
};

/*
 * Execute a user program from the RAM disk
 */
int exec(const char* filename) {
    char* p = (char*)INITRD_BASE;
    while (memcmp(p + sizeof(struct cpio_t), "TRAILER!!!", 10)) {
        struct cpio_t* hdr = (struct cpio_t*)p;
        int namesize = hextoi(hdr->namesize, 8);
        int filesize = hextoi(hdr->filesize, 8);
        int headsize = align(sizeof(struct cpio_t) + namesize, 4);
        int datasize = align(filesize, 4);
        if (!memcmp(p + sizeof(struct cpio_t), filename, namesize)) { // 從 CPIO archive 裡找到 prog.bin
            char* entry = p + headsize; // entry = p + headsize 指到 prog.bin 的實際程式內容

            unsigned long user_stack = (unsigned long)alloc_page() + STACK_SIZE; // 配一頁當 user stack, alloc_page() 回傳一頁的低位址，
            // 例如：0x80212000, stack 是往低位址長，所以初始 stack pointer 要放在這頁頂端：

            asm volatile(
                "mv t0, sp\n"
                "csrw sscratch, t0\n" // 把 kernel stack pointer 存到 sscratch，讓 trap handler 可以用 sscratch 找到 kernel stack

                "csrw sepc, %[entry]\n"
                "mv sp, %[usp]\n" // sp 指到 user stack，讓 sret 跳回 U-mode 後就有 stack 可用

                "li t0, (1 << 8)\n"
                "csrc sstatus, t0\n" // 清掉 sstatus.SPP(bit 8) 為 0，讓 sret 跳回 U-mode

                "li t0, (1 << 5)\n"
                "csrs sstatus, t0\n" // 設 sstatus.SPIE(bit 5) 為 1，讓回 U-mode 後 interrupt enable: true

                "sret\n"
                :
                : [entry] "r"(entry),
                  [usp] "r"(user_stack)
                : "t0", "memory"
                /* sret 正式切換：
                    目前：
                        privilege = S-mode
                        sepc      = prog.bin entry
                        sp        = user stack
                        sscratch  = kernel stack
                        SPP       = 0

                    執行 sret 後：
                        privilege = U-mode
                        pc        = sepc = prog.bin entry
                        sp        = user stack
                 */
            );

            return 0;
        }
        p += headsize + datasize;
    }
    return -1;
}

struct pt_regs {
    unsigned long ra;
    unsigned long sp;
    unsigned long gp;
    unsigned long tp;
    unsigned long t0;
    unsigned long t1;
    unsigned long t2;
    unsigned long s0;
    unsigned long s1;
    unsigned long a0;
    unsigned long a1;
    unsigned long a2;
    unsigned long a3;
    unsigned long a4;
    unsigned long a5;
    unsigned long a6;
    unsigned long a7;
    unsigned long s2;
    unsigned long s3;
    unsigned long s4;
    unsigned long s5;
    unsigned long s6;
    unsigned long s7;
    unsigned long s8;
    unsigned long s9;
    unsigned long s10;
    unsigned long s11;
    unsigned long t3;
    unsigned long t4;
    unsigned long t5;
    unsigned long t6;

    unsigned long sepc;
    unsigned long sstatus;
    unsigned long scause;
    unsigned long stval;
};

void do_trap(struct pt_regs *regs) {
    uart_puts("sepc: 0x");
    uart_hex(regs->sepc);
    uart_puts(", scause: 0x");
    uart_hex(regs->scause);
    uart_puts("\n");

    regs->sepc += 4; // 跳過造成 trap 的指令
}

void start_kernel() {
    uart_puts("\nStarting Lab4 kernel ...\n");
    if (exec("prog.bin"))
        uart_puts("Failed to exec user program!\n");
    while (1) {
        uart_putc(uart_getc());
    }
}
