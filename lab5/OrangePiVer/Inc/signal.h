#ifndef SIGNAL_H
#define SIGNAL_H

#include "trap.h"
#include "thread.h"

#define SIGTERM 15

void signal_task_init(struct task_struct *task);
void signal_fork(struct task_struct *parent, struct task_struct *child);

void signal_install_trampoline(void);

long sys_signal(int signum, unsigned long handler);
long sys_sigreturn(struct trap_frame *tf);
long sys_kill(long pid, int signum);

void signal_try_deliver(struct trap_frame *tf);

#endif