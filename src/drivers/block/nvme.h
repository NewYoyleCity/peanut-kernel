#ifndef NVME_H
#define NVME_H

#include "drivers/block/block.h"
#include "freelib/kstdint.h"

int nvme_init(void);
uint32_t nvme_device_count(void);
BlockDevice* nvme_get_device(uint32_t index);

#endif
