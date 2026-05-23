#ifndef RAID_H
#define RAID_H

#include "drivers/block/block.h"

#define RAID_MAX_DISKS 4

typedef struct RaidVolume {
    BlockDevice* disks[RAID_MAX_DISKS];
    uint32_t num_disks;
    uint32_t level;
    uint64_t stripe_size;
    volatile uint32_t round_robin;
} RaidVolume;

BlockDevice* raid0_create(BlockDevice** disks, uint32_t num_disks, uint64_t stripe_sectors);
BlockDevice* raid1_create(BlockDevice** disks, uint32_t num_disks);
void raid_destroy(BlockDevice* dev);
void raid_init(void);

#endif
