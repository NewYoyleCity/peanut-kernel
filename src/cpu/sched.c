/* sched.c -- Minimal cooperative/preemptive scheduler.
 *
 * Supports a fixed number of processes (PROC_MAX).  Each process saves its
 * register frame (20 qwords) so that irq_timer_dispatch can switch between
 * them on timer interrupts.  The scheduling policy is configurable:
 *   - CONFIG_SCHED_CFS: always pick the process with the smallest vruntime
 *     (a very simple Completely Fair Scheduler emulation).
 *   - default: round-robin.
 *
 * User-space preemption is enabled via sched_arm_user_preempt() and only
 * switches when the interrupted code is running at ring 3.
 *
 * Key design decisions:
 *   - The idle loop (idle_entry) runs in ring 0 with interrupts enabled.
 *   - PROC_ZOMBIE state is terminal; no wait/reap mechanism yet.
 *   - The frame layout matches the order pushed by irq.asm. */

#include "cpu/sched.h"
#include "cpu/pic.h"
#include "cpu/pit.h"
#include "config.h"
#include "freelib/kstdint.h"

#define FRAME_QWORDS 20
#define PROC_MAX CONFIG_SCHED_MAX_PROCS

#define PROC_UNUSED 0
#define PROC_RUNNABLE 1
#define PROC_ZOMBIE 2

#define SIGKILL 9
#define SIGSEGV 11
#define SIGINT  2

typedef struct {
    int state;
    int pid;
    int signal;
    uint64_t vruntime;
    uint64_t frame[FRAME_QWORDS] __attribute__((aligned(16)));
} Process;

static volatile int user_preempt_armed;
static Process procs[PROC_MAX];
static int current_proc;
static int next_pid;
volatile int sched_in_idle;
volatile uint64_t sched_jiffies;

/* idle_entry -- idle process: loops with STI; HLT, consuming no CPU.
 */
void idle_entry(void) {
    sched_in_idle = 1;
    for (;;) {
        __asm__ volatile("sti\n\thlt");
    }
}


/* memcpy64 -- copy n qwords (used for register-frame save/restore).
 */static void memcpy64(uint64_t* d, const uint64_t* s, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) d[i] = s[i];
}


/* sched_init -- initialise process table; mark all slots unused.
 */
void sched_init(void) {
    user_preempt_armed = 0;
    sched_in_idle = 0;
    current_proc = -1;
    next_pid = 1;
    for (uint32_t i = 0; i < PROC_MAX; i++) {
        procs[i].state = PROC_UNUSED;
        procs[i].pid = 0;
        procs[i].signal = 0;
        procs[i].vruntime = 0;
        for (uint32_t j = 0; j < FRAME_QWORDS; j++)
            procs[i].frame[j] = 0;
    }
}


/* sched_arm_user_preempt -- enable user-space preemption on next timer tick.
 */
void sched_arm_user_preempt(void) {
    user_preempt_armed = 1;
}


/* sched_disarm_user_preempt -- disable preemption and clear idle flag.
 */
void sched_disarm_user_preempt(void) {
    user_preempt_armed = 0;
    sched_in_idle = 0;
}


/* sched_mark_user_live -- mark that user code is executing (idle is false).
 */
void sched_mark_user_live(void) {
    sched_in_idle = 0;
}


/* alloc_proc -- find the first unused process slot.
 */static int alloc_proc(void) {
    for (uint32_t i = 0; i < PROC_MAX; i++) {
        if (procs[i].state == PROC_UNUSED)
            return (int)i;
    }
    return -1;
}


/* sched_create_user -- create a new user process with the given entry and stack.
 */
int sched_create_user(uint64_t entry, uint64_t user_rsp) {
    int slot = alloc_proc();
    if (slot < 0)
        return -1;
    Process* p = &procs[slot];
    p->state = PROC_RUNNABLE;
    p->pid = next_pid++;
    p->signal = 0;
    p->vruntime = 0;
    for (uint32_t i = 0; i < FRAME_QWORDS; i++)
        p->frame[i] = 0;
    p->frame[15] = entry;
    p->frame[16] = 0x23;
    p->frame[17] = 0x202;
    p->frame[18] = user_rsp;
    p->frame[19] = 0x1B;
    if (current_proc < 0)
        current_proc = slot;
    return p->pid;
}


/* sched_fork_current -- fork the current process; child gets a clean frame.
 */
int sched_fork_current(uint64_t user_rip, uint64_t user_rsp, uint64_t user_flags) {
    if (current_proc < 0 || procs[current_proc].state != PROC_RUNNABLE)
        return -1;
    int slot = alloc_proc();
    if (slot < 0)
        return -1;
    Process* child = &procs[slot];
    child->state = PROC_RUNNABLE;
    child->pid = next_pid++;
    child->signal = 0;
    child->vruntime = procs[current_proc].vruntime;
    for (uint32_t i = 0; i < FRAME_QWORDS; i++)
        child->frame[i] = 0;
    child->frame[14] = 0;
    child->frame[15] = user_rip;
    child->frame[16] = 0x23;
    child->frame[17] = user_flags ? user_flags : 0x202;
    child->frame[18] = user_rsp;
    child->frame[19] = 0x1B;
    return child->pid;
}


/* sched_kill -- send a signal to a process; SIGKILL moves it to ZOMBIE.
 */
int sched_kill(int pid, int signal) {
    for (uint32_t i = 0; i < PROC_MAX; i++) {
        if (procs[i].state == PROC_RUNNABLE && procs[i].pid == pid) {
            procs[i].signal = signal;
            if (signal == SIGKILL || signal == SIGSEGV || signal == SIGINT)
                procs[i].state = PROC_ZOMBIE;
            if ((int)i == current_proc)
                current_proc = -1;
            return 0;
        }
    }
    return -1;
}


/* sched_current_pid -- return PID of the currently running process.
 */
int sched_current_pid(void) {
    if (current_proc < 0 || procs[current_proc].state != PROC_RUNNABLE)
        return -1;
    return procs[current_proc].pid;
}


/* sched_runnable_count -- return number of runnable processes.
 */
int sched_runnable_count(void) {
    int n = 0;
    for (uint32_t i = 0; i < PROC_MAX; i++) {
        if (procs[i].state == PROC_RUNNABLE)
            n++;
    }
    return n;
}


/* choose_rr -- round-robin selection from the process table.
 */static int choose_rr(void) {
    int start = current_proc < 0 ? 0 : current_proc + 1;
    for (uint32_t n = 0; n < PROC_MAX; n++) {
        int idx = (start + (int)n) % PROC_MAX;
        if (procs[idx].state == PROC_RUNNABLE)
            return idx;
    }
    return -1;
}


/* choose_cfs -- select the process with the smallest vruntime.
 */static int choose_cfs(void) {
    int best = -1;
    for (uint32_t i = 0; i < PROC_MAX; i++) {
        if (procs[i].state != PROC_RUNNABLE)
            continue;
        if (best < 0 || procs[i].vruntime < procs[best].vruntime)
            best = (int)i;
    }
    return best;
}


/* choose_next -- pick next process according to configured policy.
 */static int choose_next(void) {
#ifdef CONFIG_SCHED_CFS
    return choose_cfs();
#else
    return choose_rr();
#endif
}


/* irq_timer_dispatch -- timer interrupt handler: saves current frame,
 * selects next process, returns pointer to its saved frame for iretq.
 */
uint64_t* irq_timer_dispatch(uint64_t* sp) {
    pit_tick();
    pic_master_eoi();
    sched_jiffies++;

    if (!user_preempt_armed)
        return sp;

    uint64_t cs = sp[16];
    int from_user = (cs & 3u) == 3u;
    if (!from_user)
        return sp;

    if (current_proc >= 0 && procs[current_proc].state == PROC_RUNNABLE) {
        memcpy64(procs[current_proc].frame, sp, FRAME_QWORDS);
        procs[current_proc].vruntime++;
    }

    int next = choose_next();
    if (next < 0)
        return sp;

    current_proc = next;
    return procs[current_proc].frame;
}
