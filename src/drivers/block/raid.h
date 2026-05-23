#ifndef RAID_H
#define RAID_H

#include "drivers/block/block.h"

#define RAID_MAX_DISKS 8

typedef struct RaidVolume {
    BlockDevice* disks[RAID_MAX_DISKS];
    uint32_t num_disks;
    uint32_t level;          /* 0=RAID0, 1=RAID1, 5=RAID5, 10=RAID10 */
    uint64_t stripe_size;    /* Stripe size in sectors (for RAID0/5/10) */
    volatile uint32_t round_robin;
} RaidVolume;

/* RAID level creators */
BlockDevice* raid0_create(BlockDevice** disks, uint32_t num_disks, uint64_t stripe_sectors);
BlockDevice* raid1_create(BlockDevice** disks, uint32_t num_disks);
BlockDevice* raid5_create(BlockDevice** disks, uint32_t num_disks, uint64_t stripe_sectors);
BlockDevice* raid10_create(BlockDevice** disks, uint32_t num_disks, uint64_t stripe_sectors);

/* Cleanup */
void raid_destroy(BlockDevice* dev);

/* Boot-time announcement */
void raid_init(void);

#endif
