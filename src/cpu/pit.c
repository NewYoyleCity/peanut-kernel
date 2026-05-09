#include "cpu/pit.h"
#include "drivers/bus/io.h"

#define PIT_CH0 0x40
#define PIT_CMD 0x43

void pit_init_hz(uint32_t hz) {
    if (hz == 0) hz = 100;
    uint32_t divisor = 1193182u / hz;
    if (divisor > 0xFFFFu) divisor = 0xFFFFu;
    if (divisor < 2) divisor = 2;
    outb(PIT_CMD, 0x36);
    outb(PIT_CH0, (uint8_t)(divisor & 0xFF));
    outb(PIT_CH0, (uint8_t)((divisor >> 8) & 0xFF));
}
