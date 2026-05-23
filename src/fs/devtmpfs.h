#ifndef DEVTMPFS_H
#define DEVTMPFS_H

#include "freelib/kstdint.h"

void devtmpfs_mount(void);
int devtmpfs_is_path(const char* path);
int devtmpfs_pread(const char* path, uint32_t off, uint8_t* buf, uint32_t len, uint32_t* out);
int devtmpfs_pwrite(const char* path, uint32_t off, const uint8_t* buf, uint32_t len, uint32_t* out);

#endif
