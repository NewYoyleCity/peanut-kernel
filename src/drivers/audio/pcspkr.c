/*
 * PC Speaker driver
 *
 * Uses the PIT (channel 2) to generate square-wave tones on the
 * built-in PC speaker.  This is the simplest possible sound driver:
 * no DMA, no IRQ, no buffers — just frequency control.
 *
 * The PC speaker is controlled via:
 *   - PIT counter 2 at I/O 0x42 (frequency divisor)
 *   - PIT control register at 0x43 (mode 3 = square wave)
 *   - PPI (Programmable Peripheral Interface) port 0x61, bit 1
 *     enables the speaker gate, bit 0 gates the PIT output.
 */

#include "pcspkr.h"
#include "drivers/bus/io.h"
#include "freelib/kstdio.h"

/* PIT registers */
#define PIT_CH2_DATA    0x42
#define PIT_CMD         0x43
#define PPI_PORT_B      0x61

/* PIT command: channel 2, access lo/hi, mode 3 (square wave), binary */
#define PIT_CMD_CH2_MODE3 0xB6

/* Default PIT input frequency */
#define PIT_BASE_FREQ   1193182

/*
 * Set the PC speaker frequency in Hz.
 * The PIT divisor = 1193182 / freq.
 */
static void speaker_set_freq(uint32_t freq_hz) {
    if (freq_hz == 0) freq_hz = 1;
    uint32_t divisor = PIT_BASE_FREQ / freq_hz;
    if (divisor > 65535) divisor = 65535;
    if (divisor < 1) divisor = 1;

    outb(PIT_CMD, PIT_CMD_CH2_MODE3);
    outb(PIT_CH2_DATA, (uint8_t)(divisor & 0xFF));
    outb(PIT_CH2_DATA, (uint8_t)((divisor >> 8) & 0xFF));
}

/*
 * Enable the speaker by setting PPI port B bits 0 and 1.
 */
static void speaker_enable(void) {
    uint8_t val = inb(PPI_PORT_B);
    outb(PPI_PORT_B, val | 0x03);
}

/*
 * Disable the speaker by clearing PPI port B bits 0 and 1.
 */
static void speaker_disable(void) {
    uint8_t val = inb(PPI_PORT_B);
    outb(PPI_PORT_B, val & ~0x03);
}

/*
 * Play a tone at the given frequency for the given duration.
 * The duration is in milliseconds; we busy-wait with HLT.
 */
void pcspkr_play(uint32_t freq_hz, uint32_t duration_ms) {
    speaker_set_freq(freq_hz);
    speaker_enable();

    /* Busy-wait loop; each iteration is ~1 ms on most systems */
    for (uint32_t i = 0; i < duration_ms; i++) {
        for (volatile uint32_t j = 0; j < 100000; j++) __asm__ volatile("pause");
    }

    speaker_disable();
}

/*
 * Play a beep at 880 Hz for 200 ms (standard POST-style beep).
 */
void pcspkr_beep(void) {
    pcspkr_play(880, 200);
}

/*
 * Play a simple melody (array of freq, duration pairs, terminated by 0,0).
 * Example:
 *   uint32_t melody[] = { 440, 200, 880, 200, 0, 0 };
 *   pcspkr_melody(melody);
 */
void pcspkr_melody(const uint32_t *notes) {
    if (!notes) return;
    for (uint32_t i = 0; notes[i] != 0 || notes[i+1] != 0; i += 2) {
        pcspkr_play(notes[i], notes[i+1]);
    }
}

/*
 * Initialize PC speaker (just prints a message).
 */
void pcspkr_init(void) {
    kprint("PCSPKR: PC speaker driver initialized\n");
}
