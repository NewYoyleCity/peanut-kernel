#ifndef VIRTIO_NET_H
#define VIRTIO_NET_H

#include "net.h"

void virtio_net_init(void);
net_device_t *virtio_net_get_device(void);

#endif
