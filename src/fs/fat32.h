#ifndef FAT32_H
#define FAT32_H

#include "fs/partition.h"

#define FAT32_MAX_NAME 13
#define FAT32_ATTR_DIRECTORY 0x10
#define PEANUT_FAT12 12
#define PEANUT_FAT16 16
#define PEANUT_FAT32 32

typedef struct {
    Partition partition;
    uint8_t fat_type;
    uint32_t fat_start_lba;
    uint32_t data_start_lba;
    uint32_t root_dir_lba;
    uint32_t root_dir_sectors;
    uint32_t root_cluster;
    uint32_t sectors_per_fat;
    uint8_t sectors_per_cluster;
    uint16_t root_entry_count;
} Fat32Volume;

typedef struct {
    char name[FAT32_MAX_NAME];
    uint8_t attributes;
    uint32_t first_cluster;
    uint32_t size;
} Fat32DirEntry;

int fat32_mount(Fat32Volume* volume, const Partition* partition);
uint32_t fat32_list_root(Fat32Volume* volume, Fat32DirEntry* entries, uint32_t max_entries);
int fat32_find_dir_in_root(Fat32Volume* volume, const char* dirname, Fat32DirEntry* out);
int fat32_find_in_dir(Fat32Volume* volume, uint32_t dir_cluster, const char* name, Fat32DirEntry* out);
int fat32_find_root(Fat32Volume* volume, const char* path, Fat32DirEntry* out);
int fat32_read_file(Fat32Volume* volume, const char* name, void* buffer, uint32_t buffer_size, uint32_t* bytes_read);
int fat32_write_file(Fat32Volume* volume, const char* name, const void* buffer, uint32_t size);

#endif
