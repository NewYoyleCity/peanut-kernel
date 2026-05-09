#ifndef PARTITION_H
#define PARTITION_H

#include "drivers/block/block.h"

#define PARTITION_MAX 4

typedef struct {
    BlockDevice* disk;
    uint8_t type;
    uint64_t first_lba;
    uint64_t sector_count;
} Partition;

int partition_scan_mbr(BlockDevice* disk, Partition* out, uint32_t max_partitions);
int partition_is_fat32(const Partition* partition);
int partition_is_fat(const Partition* partition);

#endif
