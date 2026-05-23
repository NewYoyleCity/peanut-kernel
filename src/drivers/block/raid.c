#include "drivers/block/raid.h"
#include "freelib/kalloc.h"
#include "freelib/kpanic.h"
#include "freelib/kstdio.h"

static void copy_mem(void* dst, const void* src, uint64_t n) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    for (uint64_t i = 0; i < n; i++)
        d[i] = s[i];
}

static int raid_read(BlockDevice* dev, uint64_t lba, uint32_t count, void* buffer);
static int raid_write(BlockDevice* dev, uint64_t lba, uint32_t count, const void* buffer);
static int raid1_read(BlockDevice* dev, uint64_t lba, uint32_t count, void* buffer);
static int raid1_write(BlockDevice* dev, uint64_t lba, uint32_t count, const void* buffer);

static int raid0_read(BlockDevice* dev, uint64_t lba, uint32_t count, void* buffer) {
    RaidVolume* vol = (RaidVolume*)dev->driver_data;
    if (!vol || vol->num_disks < 2) return -1;
    uint64_t stripe = vol->stripe_size;
    if (stripe == 0) stripe = 1;

    for (uint32_t i = 0; i < count; i++) {
        uint64_t global = lba + i;
        uint64_t stripe_idx = global / stripe;
        uint64_t disk_idx = stripe_idx % vol->num_disks;
        uint64_t stripe_off = global % stripe;
        uint64_t disk_lba = (stripe_idx / vol->num_disks) * stripe + stripe_off;

        int r = block_read(vol->disks[disk_idx], disk_lba, 1,
                           (uint8_t*)buffer + i * BLOCK_SECTOR_SIZE);
        if (r != 0) return r;
    }
    return 0;
}

static int raid0_write(BlockDevice* dev, uint64_t lba, uint32_t count, const void* buffer) {
    RaidVolume* vol = (RaidVolume*)dev->driver_data;
    if (!vol || vol->num_disks < 2) return -1;
    uint64_t stripe = vol->stripe_size;
    if (stripe == 0) stripe = 1;

    for (uint32_t i = 0; i < count; i++) {
        uint64_t global = lba + i;
        uint64_t stripe_idx = global / stripe;
        uint64_t disk_idx = stripe_idx % vol->num_disks;
        uint64_t stripe_off = global % stripe;
        uint64_t disk_lba = (stripe_idx / vol->num_disks) * stripe + stripe_off;

        int r = block_write(vol->disks[disk_idx], disk_lba, 1,
                            (const uint8_t*)buffer + i * BLOCK_SECTOR_SIZE);
        if (r != 0) return r;
    }
    return 0;
}

static int raid1_read(BlockDevice* dev, uint64_t lba, uint32_t count, void* buffer) {
    RaidVolume* vol = (RaidVolume*)dev->driver_data;
    if (!vol || vol->num_disks < 2) return -1;

    uint32_t start = __sync_fetch_and_add(&vol->round_robin, 1) % vol->num_disks;

    for (uint32_t attempt = 0; attempt < vol->num_disks; attempt++) {
        uint32_t idx = (start + attempt) % vol->num_disks;
        int r = block_read(vol->disks[idx], lba, count, buffer);
        if (r == 0) return 0;
    }
    return -1;
}

static int raid1_write(BlockDevice* dev, uint64_t lba, uint32_t count, const void* buffer) {
    RaidVolume* vol = (RaidVolume*)dev->driver_data;
    if (!vol || vol->num_disks < 2) return -1;

    for (uint32_t i = 0; i < vol->num_disks; i++) {
        int r = block_write(vol->disks[i], lba, count, buffer);
        if (r != 0) return r;
    }
    return 0;
}

static int raid_read(BlockDevice* dev, uint64_t lba, uint32_t count, void* buffer) {
    RaidVolume* vol = (RaidVolume*)dev->driver_data;
    if (!vol) return -1;
    if (lba + count > dev->sector_count) return -1;
    if (vol->level == 0) return raid0_read(dev, lba, count, buffer);
    return raid1_read(dev, lba, count, buffer);
}

static int raid_write(BlockDevice* dev, uint64_t lba, uint32_t count, const void* buffer) {
    RaidVolume* vol = (RaidVolume*)dev->driver_data;
    if (!vol) return -1;
    if (lba + count > dev->sector_count) return -1;
    if (vol->level == 0) return raid0_write(dev, lba, count, buffer);
    return raid1_write(dev, lba, count, buffer);
}

BlockDevice* raid0_create(BlockDevice** disks, uint32_t num_disks, uint64_t stripe_sectors) {
    if (!disks || num_disks < 2 || num_disks > RAID_MAX_DISKS) return NULL;
    if (stripe_sectors == 0) stripe_sectors = 1;

    for (uint32_t i = 0; i < num_disks; i++)
        if (!disks[i] || !disks[i]->read || !disks[i]->write) return NULL;

    RaidVolume* vol = (RaidVolume*)kalloc(sizeof(RaidVolume));
    if (!vol) return NULL;
    for (uint32_t i = 0; i < num_disks; i++)
        vol->disks[i] = disks[i];
    vol->num_disks = num_disks;
    vol->level = 0;
    vol->stripe_size = stripe_sectors;
    vol->round_robin = 0;

    uint64_t min_sectors = disks[0]->sector_count;
    for (uint32_t i = 1; i < num_disks; i++)
        if (disks[i]->sector_count < min_sectors)
            min_sectors = disks[i]->sector_count;

    uint64_t stripe_group = stripe_sectors * num_disks;
    uint64_t total = (min_sectors / stripe_group) * stripe_group;

    BlockDevice* dev = (BlockDevice*)kalloc(sizeof(BlockDevice));
    if (!dev) {
        kfree(vol);
        return NULL;
    }
    dev->name = "raid0";
    dev->sector_count = total;
    dev->sector_size = BLOCK_SECTOR_SIZE;
    dev->driver_data = vol;
    dev->read = raid_read;
    dev->write = raid_write;
    return dev;
}

BlockDevice* raid1_create(BlockDevice** disks, uint32_t num_disks) {
    if (!disks || num_disks < 2 || num_disks > RAID_MAX_DISKS) return NULL;

    for (uint32_t i = 0; i < num_disks; i++)
        if (!disks[i] || !disks[i]->read || !disks[i]->write) return NULL;

    RaidVolume* vol = (RaidVolume*)kalloc(sizeof(RaidVolume));
    if (!vol) return NULL;
    for (uint32_t i = 0; i < num_disks; i++)
        vol->disks[i] = disks[i];
    vol->num_disks = num_disks;
    vol->level = 1;
    vol->stripe_size = 0;
    vol->round_robin = 0;

    uint64_t min_sectors = disks[0]->sector_count;
    for (uint32_t i = 1; i < num_disks; i++)
        if (disks[i]->sector_count < min_sectors)
            min_sectors = disks[i]->sector_count;

    BlockDevice* dev = (BlockDevice*)kalloc(sizeof(BlockDevice));
    if (!dev) {
        kfree(vol);
        return NULL;
    }
    dev->name = "raid1";
    dev->sector_count = min_sectors;
    dev->sector_size = BLOCK_SECTOR_SIZE;
    dev->driver_data = vol;
    dev->read = raid_read;
    dev->write = raid_write;
    return dev;
}

void raid_destroy(BlockDevice* dev) {
    if (!dev) return;
    RaidVolume* vol = (RaidVolume*)dev->driver_data;
    if (vol) kfree(vol);
    kfree(dev);
}

void raid_init(void) {
    kprint_timed("Software RAID available (RAID0/RAID1)\n");
}
