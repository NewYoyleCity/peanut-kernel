#ifndef TTY_H
#define TTY_H

#include "freelib/kstdint.h"

void tty_init(void);
int tty_read_char(char* out);
int tty_write_char(char c);
int tty_can_read(void);

#endif
