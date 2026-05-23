#ifndef PIT_H
#define PIT_H

#include "freelib/kstdint.h"

#define PIT_HZ_DEFAULT 100

void pit_init_hz(uint32_t hz);
void pit_tick(void);
uint64_t pit_uptime_ms(void);
uint64_t pit_uptime_ticks(void);

#endif
