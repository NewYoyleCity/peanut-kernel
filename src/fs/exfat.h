#ifndef EXFAT_H
#define EXFAT_H

#include "fs/partition.h"

#define EXFAT_MAX_NAME 256
#define EXFAT_ATTR_DIRECTORY 0x10

typedef struct {
    Partition partition;
    uint32_t fat_start_lba;
    uint32_t cluster_heap_lba;
    uint32_t root_cluster;
    uint32_t cluster_count;
    uint8_t sectors_per_cluster;
} ExfatVolume;

typedef struct {
    char name[EXFAT_MAX_NAME];
    uint8_t attributes;
    uint32_t first_cluster;
    uint64_t size;
} ExfatDirEntry;

int exfat_probe_boot(BlockDevice* disk, uint64_t first_lba);
int exfat_mount(ExfatVolume* volume, const Partition* partition);
int exfat_find_root(ExfatVolume* volume, const char* path, ExfatDirEntry* out);
int exfat_read_file(ExfatVolume* volume, const char* name, void* buffer, uint32_t buffer_size, uint32_t* bytes_read);
int exfat_write_file(ExfatVolume* volume, const char* name, const void* buffer, uint32_t buffer_size);
int exfat_dir_exists(ExfatVolume* volume, const char* dirname);

#endif
