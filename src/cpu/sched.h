#ifndef SCHED_H
#define SCHED_H

#include "freelib/kstdint.h"

void sched_init(void);
void sched_arm_user_preempt(void);
void sched_disarm_user_preempt(void);
void sched_mark_user_live(void);
int sched_create_user(uint64_t entry, uint64_t user_rsp);
int sched_fork_current(uint64_t user_rip, uint64_t user_rsp, uint64_t user_flags);
int sched_kill(int pid, int signal);
int sched_current_pid(void);
int sched_runnable_count(void);
void idle_entry(void);
uint64_t* irq_timer_dispatch(uint64_t* sp);

#endif
