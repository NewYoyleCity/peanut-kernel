#ifndef SB16_H
#define SB16_H

#include "freelib/kstdint.h"

/* Initialize the SB16 DSP (must be called before playback) */
void sb16_init(void);

/* Start 8-bit unsigned PCM playback via DMA */
int sb16_play_8bit(uint8_t *buffer, uint32_t length, uint32_t sample_rate);

/* Start 16-bit signed PCM playback via DMA */
int sb16_play_16bit(uint8_t *buffer, uint32_t length, uint32_t sample_rate);

/* Stop all playback immediately */
void sb16_stop(void);

/* Returns non-zero if a SB16 was detected and initialized */
int sb16_is_present(void);

#endif
