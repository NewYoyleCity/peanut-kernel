#ifndef PCSPKR_H
#define PCSPKR_H

#include "freelib/kstdint.h"

/* Initialize the PC speaker driver */
void pcspkr_init(void);

/* Play a tone at the given frequency (Hz) for the given duration (ms) */
void pcspkr_play(uint32_t freq_hz, uint32_t duration_ms);

/* Play a standard 880 Hz, 200 ms beep */
void pcspkr_beep(void);

/* Play a melody from a NULL-terminated array of (freq, duration) pairs */
void pcspkr_melody(const uint32_t *notes);

#endif
