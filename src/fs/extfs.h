#ifndef EXTFS_H
#define EXTFS_H

#include "fs/partition.h"

typedef struct {
    Partition partition;
    uint32_t block_size;
    uint32_t blocks_per_group;
    uint32_t inodes_per_group;
    uint32_t inode_size;
    uint32_t first_data_block;
    uint32_t group_desc_lba;
    uint32_t group_desc_off;
    uint32_t groups;
} ExtVolume;

typedef struct {
    uint32_t inode;
    uint8_t file_type;
    char name[256];
} ExtDirEntry;

int extfs_probe(BlockDevice* disk, uint64_t first_lba);
int extfs_mount(ExtVolume* v, const Partition* p);
int extfs_find_root(ExtVolume* v, const char* path, uint32_t* out_inode);
int extfs_stat(ExtVolume* v, const char* path, uint64_t* size, int* is_dir);
int extfs_read_file(ExtVolume* v, const char* path, void* buffer, uint32_t buffer_size, uint32_t* bytes_read);
int extfs_write_file(ExtVolume* v, const char* path, const void* buffer, uint32_t buffer_size);
int extfs_dir_exists(ExtVolume* v, const char* path);

#endif
