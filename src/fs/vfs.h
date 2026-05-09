#ifndef VFS_H
#define VFS_H

#include "freelib/kstdint.h"

void vfs_init(void);
void vfs_mount_devtmpfs(void);
int vfs_is_pseudo_path(const char* path);
int vfs_pseudo_read(const char* path, uint8_t* buf, uint32_t len, uint32_t* out);
int vfs_pseudo_pread(const char* path, uint32_t off, uint8_t* buf, uint32_t len, uint32_t* out);

#endif
