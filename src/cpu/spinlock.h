/* spinlock.h -- Simple test-and-set spinlock.
 *
 * Uses GCC built-in __sync_lock_test_and_set for atomic acquire
 * and __sync_lock_release for release.  Includes a PAUSE instruction
 * in the spin loop to reduce power consumption on Hyper-Threaded CPUs.
 */

#ifndef SPINLOCK_H
#define SPINLOCK_H

#include "freelib/kstdint.h"

typedef struct {
    volatile uint32_t v;
} spinlock_t;

static inline void spin_init(spinlock_t* s) {
    s->v = 0;
}

static inline void spin_lock(spinlock_t* s) {
    for (;;) {
        if (__sync_lock_test_and_set(&s->v, 1u) == 0u)
            return;
        __asm__ volatile("pause" ::: "memory");
    }
}

static inline void spin_unlock(spinlock_t* s) {
    __sync_lock_release(&s->v);
}

#endif
