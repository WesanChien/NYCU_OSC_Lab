#include "shell.h"
#include "uart.h"
#include "kstring.h"
#include "sbi.h"
#include "initrd.h"
#include "loader.h"
#include "mm.h"
#include "exec.h"
#include "timer.h"
#include "irq.h"
#include "task.h"
#include "thread_test.h"
#include "user_test.h"

#define TIMEOUT_MSG_MAX 16
#define TIMEOUT_MSG_LEN 128

struct timeout_msg {
    int used;
    unsigned long created_at;
    char msg[TIMEOUT_MSG_LEN];
};

static struct timeout_msg timeout_msgs[TIMEOUT_MSG_MAX];

static int is_digit(char c) {
    return c >= '0' && c <= '9';
}

static void copy_str_limit(char *dst, const char *src, int max_len) {
    int i = 0;

    while (src[i] && i < max_len - 1) {
        dst[i] = src[i];
        i++;
    }

    dst[i] = '\0';
}

static struct timeout_msg *timeout_msg_alloc(void) { // 從 timeout_msgs[] 找一個沒被使用的位置, 分配給 setTimeout command 用
    unsigned long s = local_irq_save();

    for (int i = 0; i < TIMEOUT_MSG_MAX; i++) {
        if (!timeout_msgs[i].used) {
            timeout_msgs[i].used = 1;
            local_irq_restore(s);
            return &timeout_msgs[i];
        }
    }

    local_irq_restore(s);
    return 0;
}

static void timeout_msg_free(struct timeout_msg *m) {
    unsigned long s = local_irq_save();
    m->used = 0;
    local_irq_restore(s);
}

static int parse_uint(const char **p, int *out) {
    int value = 0;
    const char *s = *p;

    if (!is_digit(*s))
        return -1;

    while (is_digit(*s)) {
        value = value * 10 + (*s - '0');
        s++;
    }

    *out = value;
    *p = s;
    return 0;
}

static void set_timeout_callback(void *arg) { // timer 到期後會呼叫這個 function
    struct timeout_msg *m = (struct timeout_msg *)arg;

    uart_puts("[setTimeout] created at ");
    uart_dec(m->created_at);
    uart_puts("s, fired at ");
    uart_dec(timer_get_boot_time());
    uart_puts("s: ");
    uart_puts(m->msg);
    uart_puts("\n");

    timeout_msg_free(m);
}

static void print_help(void) {
    uart_puts("Available commands:\n");
    uart_puts("  help        - show this help message\n");
    uart_puts("  hello       - print Hello World!\n");
    uart_puts("  info        - show OpenSBI information\n");
    uart_puts("  ls          - list files in initramfs\n");
    uart_puts("  cat <file>  - show file content from initramfs\n");
    uart_puts("  load        - load a kernel image over UART and boot it\n");
    uart_puts("  memdump     - show memory allocator state\n");
    uart_puts("  memtest     - run memory allocator test\n");
    uart_puts("  exec <file> - execute a user program from initramfs\n");
    uart_puts("  setTimeout <sec> <msg> - print message after seconds\n");
    uart_puts("  tasktest    - run advanced exercise 2 task queue test\n");
    uart_puts("  threadtest  - run thread test\n");
    uart_puts("  usertest    - run user process test\n");
}

void shell_run(unsigned long fdt_addr) {
    char buf[128];
    int idx;

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
            initrd_list();
        } else if (starts_with(buf, "cat")) {
            const char *name = skip_spaces(buf + 3);

            if (*name == '\0')
                uart_puts("Usage: cat <file>\n");
            else
                initrd_cat(name);
        } else if (str_eq(buf, "load")) {
            load_image_and_boot(fdt_addr);
        } else if (str_eq(buf, "memdump")) {
            mm_dump();
        } else if (str_eq(buf, "memtest")) {
            mm_test();
        } else if (starts_with(buf, "exec")  && (buf[4] == '\0' || buf[4] == ' ' || buf[4] == '\t')) {
            const char *name = skip_spaces(buf + 4);
            
            if (*name == '\0')
                uart_puts("Usage: exec <file>\n");
            else
                exec_user_program(name);
        } else if (starts_with(buf, "setTimeout") && (buf[10] == '\0' || buf[10] == ' ' || buf[10] == '\t')) {
            const char *p = skip_spaces(buf + 10);
            int sec = 0;

            if (parse_uint(&p, &sec) < 0) {
                uart_puts("Usage: setTimeout <sec> <msg>\n");
            } else { // 解析 sec 成功, p 現在指向 msg 的開頭 (或是空字串)
                p = skip_spaces(p);
            
                if (*p == '\0') {
                    uart_puts("Usage: setTimeout <sec> <msg>\n");
                } else {
                    struct timeout_msg *m = timeout_msg_alloc(); // 從 timeout_msgs[] 找一個沒被使用的位置, 分配給 setTimeout command 用
                
                    if (!m) {
                        uart_puts("[setTimeout] no free message slot\n");
                    } else {
                        m->created_at = timer_get_boot_time();
                        copy_str_limit(m->msg, p, TIMEOUT_MSG_LEN); // 把 msg 複製到 m->msg 裡
                    
                        if (add_timer(set_timeout_callback, m, sec) < 0) {
                            timeout_msg_free(m);
                            uart_puts("[setTimeout] failed to add timer\n");
                        } else {
                            uart_puts("[setTimeout] registered\n");
                        }
                    }
                }
            }     
        } else if (str_eq(buf, "tasktest")) {
            task_queue_adv2_test();
        } else if (str_eq(buf, "threadtest")) {
            run_thread_test();
        } else if (str_eq(buf, "usertest")) {
            run_user_test();
        } else if (buf[0] != '\0') {
            uart_puts("Unknown command: ");
            uart_puts(buf);
            uart_puts("\n");
        }
    }
}