#ifndef ENTROPY_H
#define ENTROPY_H

#include "freelib/kstdint.h"

void entropy_init(void);
void entropy_mix(uint64_t value);
uint8_t entropy_random_byte(void);
uint8_t entropy_urandom_byte(void);
int entropy_has_hardware_rng(void);

#endif
