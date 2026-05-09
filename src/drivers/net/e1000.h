#ifndef E1000_H
#define E1000_H

#include "net.h"

void e1000_init(void);
net_device_t *e1000_get_device(void);

#endif
