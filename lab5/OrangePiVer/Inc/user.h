#ifndef USER_H
#define USER_H

struct task_struct;

struct task_struct *user_process_create(void (*entry)(void));
void user_process_entry(void);

#endif