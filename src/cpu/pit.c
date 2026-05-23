#include "cpu/pit.h"
#include "drivers/bus/io.h"
#include "freelib/kstdio.h"

#define PIT_CH0 0x40
#define PIT_CMD 0x43

static uint64_t pit_tick_count;
static uint32_t pit_hz;

void pit_init_hz(uint32_t hz) {
    if (hz == 0) hz = PIT_HZ_DEFAULT;
    uint32_t divisor = 1193182u / hz;
    if (divisor > 0xFFFFu) divisor = 0xFFFFu;
    if (divisor < 2) divisor = 2;
    outb(PIT_CMD, 0x36);
    outb(PIT_CH0, (uint8_t)(divisor & 0xFF));
    outb(PIT_CH0, (uint8_t)((divisor >> 8) & 0xFF));
    pit_hz = hz;
    pit_tick_count = 0;
}

void pit_tick(void) {
    pit_tick_count++;
}

uint64_t pit_uptime_ticks(void) {
    return pit_tick_count;
}

uint64_t pit_uptime_ms(void) {
    return (pit_tick_count * 1000u) / pit_hz;
}
