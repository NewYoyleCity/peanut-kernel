#ifndef IDE_H
#define IDE_H

#include "drivers/block/block.h"

#define IDE_MAX_DEVICES 4

int ide_init();
uint32_t ide_device_count();
BlockDevice* ide_get_device(uint32_t index);

#endif
