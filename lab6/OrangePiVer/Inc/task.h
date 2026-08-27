#ifndef TASK_H
#define TASK_H

typedef void (*task_callback_t)(void *arg);

void task_init(void);
int add_task(task_callback_t callback, void *arg, int priority);
void run_pending_task(void);

void task_queue_adv2_test(void);

#endif