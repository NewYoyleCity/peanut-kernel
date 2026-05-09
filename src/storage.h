#ifndef STORAGE_H
#define STORAGE_H

#include "fs/extfs.h"
#include "fs/exfat.h"
#include "fs/fat32.h"

typedef struct {
    int fs_kind;
    Fat32Volume fat32;
    ExfatVolume exfat;
    ExtVolume ext;
} PeanutVolume;

#define PEANUT_FS_FAT32 0
#define PEANUT_FS_EXFAT 1
#define PEANUT_FS_EXT   2

void storage_init_required();
PeanutVolume* storage_get_root_volume();

#endif
