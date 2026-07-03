#ifndef USER_H
#define USER_H

#include "trap.h"
#include "thread.h"

struct task_struct;

struct task_struct *user_process_create(void (*entry)(void));
struct task_struct *user_process_create_from_file(const char *path);

void user_process_entry(void);

long user_fork(struct trap_frame *parent_tf);
int user_exec_current(const char *path, struct trap_frame *tf);

#endif