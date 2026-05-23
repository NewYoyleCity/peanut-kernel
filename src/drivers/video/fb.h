#ifndef FB_H
#define FB_H

#include "freelib/kstdint.h"

int fb_init_direct(void);
void fb_init_from_multiboot(uint64_t mbi);
int fb_ready(void);
void fb_putc(char c);
void fb_clear(void);

#endif
