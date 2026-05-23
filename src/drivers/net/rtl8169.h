#ifndef RTL8169_H
#define RTL8169_H

#include "net.h"

void rtl8169_init(void);
net_device_t *rtl8169_get_device(void);

#endif
