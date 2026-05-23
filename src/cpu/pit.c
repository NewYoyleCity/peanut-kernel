/* pit.c -- 8253/8254 Programmable Interval Timer driver.
 *
 * Configures PIT channel 0 in mode 2 (rate generator) at a configurable
 * frequency (default 100 Hz).  The kernel uses pit_tick() as the timer
 * tick callback (called from the IRQ 0 handler) and exports uptime in
 * both ticks and milliseconds.
 *
 * Design note: the divisor is derived from the PIT base frequency of
 * 1,193,182 Hz.  Values outside [2, 65535] are clamped. */

#include "cpu/pit.h"
#include "drivers/bus/io.h"
#include "freelib/kstdio.h"

#define PIT_CH0 0x40
#define PIT_CMD 0x43

static uint64_t pit_tick_count;
static uint32_t pit_hz;


/* pit_init_hz -- program PIT channel 0 at the given frequency.
 */
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


/* pit_tick -- increment tick counter (called from timer IRQ dispatch).
 */
void pit_tick(void) {
    pit_tick_count++;
}


/* pit_uptime_ticks -- return total timer ticks since boot.
 */
uint64_t pit_uptime_ticks(void) {
    return pit_tick_count;
}


/* pit_uptime_ms -- return approximate uptime in milliseconds.
 */
uint64_t pit_uptime_ms(void) {
    return (pit_tick_count * 1000u) / pit_hz;
}
