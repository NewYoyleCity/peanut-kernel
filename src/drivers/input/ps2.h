#ifndef PS2_H
#define PS2_H

#include "freelib/kstdint.h"

void ps2_init(void);
char ps2_get_char(void);
int ps2_poll_char(char* out);
int ps2_poll_mouse_packet(uint8_t packet[3]);

#endif
