#ifndef TIMER_H
#define TIMER_H

typedef void (*timer_callback_t)(void *arg);

void timer_handle_interrupt(void);
unsigned long timer_get_boot_time(void);
void timer_init(void);

int add_timer(timer_callback_t callback, void *arg, int sec);
void timer_trigger_now(void);

#endif