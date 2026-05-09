#ifndef INITRAMFS_H
#define INITRAMFS_H

#include "freelib/kstdint.h"

void initramfs_init(void);
int initramfs_find(const char* path, const uint8_t** data, uint32_t* size);

#endif
