#ifndef AHCI_H
#define AHCI_H

#include "drivers/block/block.h"

#define AHCI_MAX_DEVICES 8

int ahci_init();
uint32_t ahci_device_count();
BlockDevice* ahci_get_device(uint32_t index);

#endif
