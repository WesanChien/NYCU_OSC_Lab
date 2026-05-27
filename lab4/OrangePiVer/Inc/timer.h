#ifndef TIMER_H
#define TIMER_H

void timer_handle_interrupt(void);
unsigned long timer_get_boot_time(void);
void timer_init(void);

#endif