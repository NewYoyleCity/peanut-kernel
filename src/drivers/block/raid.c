/*
 * Software RAID driver supporting RAID0 (striping), RAID1 (mirroring),
 * RAID5 (striping with distributed parity), and RAID10 (striped mirrors).
 *
 * All levels expose a standard BlockDevice interface so the VFS and
 * storage layer can treat them identically to physical disks.
 */

#include "drivers/block/raid.h"
#include "freelib/kalloc.h"
#include "freelib/kpanic.h"
#include "freelib/kstdio.h"

/*
 * Minimal memcpy; the kernel has no libc, so we roll our own.
 */
static void copy_mem(void* dst, const void* src, uint64_t n) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    for (uint64_t i = 0; i < n; i++)
        d[i] = s[i];
}

/*
 * XOR parity calculation for RAID5: dest[i] ^= src[i] for all i.
 */
static void xor_block(void* dest, const void* src, uint32_t bytes) {
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;
    for (uint32_t i = 0; i < bytes; i++)
        d[i] ^= s[i];
}

/* Forward declarations of top-level dispatch */
static int raid_read(BlockDevice* dev, uint64_t lba, uint32_t count, void* buffer);
static int raid_write(BlockDevice* dev, uint64_t lba, uint32_t count, const void* buffer);

/* ------------------------------------------------------------------ */
/*  RAID0  — simple striping                                          */
/* ------------------------------------------------------------------ */
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

/* ------------------------------------------------------------------ */
/*  RAID1  — mirroring with round-robin reads                         */
/* ------------------------------------------------------------------ */
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

/* ------------------------------------------------------------------ */
/*  RAID5  — striping with distributed parity (left-symmetric layout) */
/*          Parity rotates across disks: P = D0 ^ D1 ^ ... ^ D(n-1)  */
/* ------------------------------------------------------------------ */
static int raid5_read(BlockDevice* dev, uint64_t lba, uint32_t count, void* buffer) {
    RaidVolume* vol = (RaidVolume*)dev->driver_data;
    if (!vol || vol->num_disks < 3) return -1;
    uint32_t nd = vol->num_disks;
    uint64_t stripe = vol->stripe_size;
    if (stripe == 0) stripe = 1;
    uint64_t data_disks = nd - 1;

    for (uint32_t i = 0; i < count; i++) {
        uint64_t global = lba + i;
        uint64_t stripe_idx = global / stripe;
        uint64_t parity_disk = (nd - 1) - (stripe_idx % nd);
        uint64_t data_stripe = stripe_idx;
        uint64_t disk_idx = data_stripe % data_disks;
        if (disk_idx >= parity_disk) disk_idx++;
        uint64_t stripe_off = global % stripe;
        uint64_t disk_lba = (stripe_idx / nd) * stripe + stripe_off;

        int r = block_read(vol->disks[disk_idx], disk_lba, 1,
                           (uint8_t*)buffer + i * BLOCK_SECTOR_SIZE);
        if (r != 0) return r;
    }
    return 0;
}

static int raid5_write(BlockDevice* dev, uint64_t lba, uint32_t count, const void* buffer) {
    RaidVolume* vol = (RaidVolume*)dev->driver_data;
    if (!vol || vol->num_disks < 3) return -1;
    uint32_t nd = vol->num_disks;
    uint64_t stripe = vol->stripe_size;
    if (stripe == 0) stripe = 1;
    uint64_t data_disks = nd - 1;

    for (uint32_t i = 0; i < count; i++) {
        uint64_t global = lba + i;
        uint64_t stripe_idx = global / stripe;
        uint64_t parity_disk = (nd - 1) - (stripe_idx % nd);
        uint64_t data_stripe = stripe_idx;
        uint64_t disk_idx = data_stripe % data_disks;
        if (disk_idx >= parity_disk) disk_idx++;
        uint64_t stripe_off = global % stripe;
        uint64_t disk_lba = (stripe_idx / nd) * stripe + stripe_off;

        int r = block_write(vol->disks[disk_idx], disk_lba, 1,
                            (const uint8_t*)buffer + i * BLOCK_SECTOR_SIZE);
        if (r != 0) return r;

        /* Compute parity: read all data blocks and XOR */
        uint8_t parity[BLOCK_SECTOR_SIZE];
        uint32_t first = 1;
        for (uint32_t j = 0; j < nd; j++) {
            if (j == parity_disk) continue;
            uint64_t pd = (stripe_idx / nd) * stripe + stripe_off;
            if (first) {
                block_read(vol->disks[j], pd, 1, parity);
                first = 0;
            } else {
                uint8_t tmp[BLOCK_SECTOR_SIZE];
                block_read(vol->disks[j], pd, 1, tmp);
                xor_block(parity, tmp, BLOCK_SECTOR_SIZE);
            }
        }
        block_write(vol->disks[parity_disk], disk_lba, 1, parity);
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/*  RAID10 — striped mirrors (RAID1+0)                                */
/*           Disks are paired: (0,1), (2,3), ... each a mirror;       */
/*           data is striped across the mirror pairs.                  */
/* ------------------------------------------------------------------ */
static int raid10_read(BlockDevice* dev, uint64_t lba, uint32_t count, void* buffer) {
    RaidVolume* vol = (RaidVolume*)dev->driver_data;
    if (!vol || vol->num_disks < 4 || (vol->num_disks & 1)) return -1;
    uint32_t mirrors = vol->num_disks / 2;
    uint64_t stripe = vol->stripe_size;
    if (stripe == 0) stripe = 1;

    for (uint32_t i = 0; i < count; i++) {
        uint64_t global = lba + i;
        uint64_t stripe_idx = global / stripe;
        uint32_t mirror = stripe_idx % mirrors;
        uint64_t stripe_off = global % stripe;
        uint64_t disk_lba = (stripe_idx / mirrors) * stripe + stripe_off;
        uint32_t disk_a = mirror * 2;
        uint32_t disk_b = mirror * 2 + 1;

        int r = block_read(vol->disks[disk_a], disk_lba, 1,
                           (uint8_t*)buffer + i * BLOCK_SECTOR_SIZE);
        if (r != 0)
            r = block_read(vol->disks[disk_b], disk_lba, 1,
                           (uint8_t*)buffer + i * BLOCK_SECTOR_SIZE);
        if (r != 0) return r;
    }
    return 0;
}

static int raid10_write(BlockDevice* dev, uint64_t lba, uint32_t count, const void* buffer) {
    RaidVolume* vol = (RaidVolume*)dev->driver_data;
    if (!vol || vol->num_disks < 4 || (vol->num_disks & 1)) return -1;
    uint32_t mirrors = vol->num_disks / 2;
    uint64_t stripe = vol->stripe_size;
    if (stripe == 0) stripe = 1;

    for (uint32_t i = 0; i < count; i++) {
        uint64_t global = lba + i;
        uint64_t stripe_idx = global / stripe;
        uint32_t mirror = stripe_idx % mirrors;
        uint64_t stripe_off = global % stripe;
        uint64_t disk_lba = (stripe_idx / mirrors) * stripe + stripe_off;
        uint32_t disk_a = mirror * 2;
        uint32_t disk_b = mirror * 2 + 1;

        int r = block_write(vol->disks[disk_a], disk_lba, 1,
                            (const uint8_t*)buffer + i * BLOCK_SECTOR_SIZE);
        if (r == 0)
            block_write(vol->disks[disk_b], disk_lba, 1,
                        (const uint8_t*)buffer + i * BLOCK_SECTOR_SIZE);
        if (r != 0) return r;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Top-level dispatch                                                */
/* ------------------------------------------------------------------ */
static int raid_read(BlockDevice* dev, uint64_t lba, uint32_t count, void* buffer) {
    RaidVolume* vol = (RaidVolume*)dev->driver_data;
    if (!vol) return -1;
    if (lba + count > dev->sector_count) return -1;
    switch (vol->level) {
        case 0:  return raid0_read(dev, lba, count, buffer);
        case 1:  return raid1_read(dev, lba, count, buffer);
        case 5:  return raid5_read(dev, lba, count, buffer);
        case 10: return raid10_read(dev, lba, count, buffer);
        default: return -1;
    }
}

static int raid_write(BlockDevice* dev, uint64_t lba, uint32_t count, const void* buffer) {
    RaidVolume* vol = (RaidVolume*)dev->driver_data;
    if (!vol) return -1;
    if (lba + count > dev->sector_count) return -1;
    switch (vol->level) {
        case 0:  return raid0_write(dev, lba, count, buffer);
        case 1:  return raid1_write(dev, lba, count, buffer);
        case 5:  return raid5_write(dev, lba, count, buffer);
        case 10: return raid10_write(dev, lba, count, buffer);
        default: return -1;
    }
}

/* ------------------------------------------------------------------ */
/*  RAID volume creators                                              */
/* ------------------------------------------------------------------ */
static BlockDevice* raid_alloc_device(RaidVolume* vol, const char* name, uint64_t sectors) {
    BlockDevice* dev = (BlockDevice*)kalloc(sizeof(BlockDevice));
    if (!dev) { kfree(vol); return NULL; }
    dev->name = name;
    dev->sector_count = sectors;
    dev->sector_size = BLOCK_SECTOR_SIZE;
    dev->driver_data = vol;
    dev->read = raid_read;
    dev->write = raid_write;
    return dev;
}

static uint64_t min_sectors(BlockDevice** disks, uint32_t num_disks) {
    uint64_t m = disks[0]->sector_count;
    for (uint32_t i = 1; i < num_disks; i++)
        if (disks[i]->sector_count < m)
            m = disks[i]->sector_count;
    return m;
}

static int validate_disks(BlockDevice** disks, uint32_t num_disks) {
    if (!disks || num_disks < 2 || num_disks > RAID_MAX_DISKS) return -1;
    for (uint32_t i = 0; i < num_disks; i++)
        if (!disks[i] || !disks[i]->read || !disks[i]->write) return -1;
    return 0;
}

BlockDevice* raid0_create(BlockDevice** disks, uint32_t num_disks, uint64_t stripe_sectors) {
    if (validate_disks(disks, num_disks) != 0) return NULL;
    if (stripe_sectors == 0) stripe_sectors = 1;

    RaidVolume* vol = (RaidVolume*)kalloc(sizeof(RaidVolume));
    if (!vol) return NULL;
    for (uint32_t i = 0; i < num_disks; i++) vol->disks[i] = disks[i];
    vol->num_disks = num_disks;
    vol->level = 0;
    vol->stripe_size = stripe_sectors;
    vol->round_robin = 0;

    uint64_t m = min_sectors(disks, num_disks);
    uint64_t stripe_group = stripe_sectors * num_disks;
    uint64_t total = (m / stripe_group) * stripe_group;

    return raid_alloc_device(vol, "raid0", total);
}

BlockDevice* raid1_create(BlockDevice** disks, uint32_t num_disks) {
    if (validate_disks(disks, num_disks) != 0) return NULL;

    RaidVolume* vol = (RaidVolume*)kalloc(sizeof(RaidVolume));
    if (!vol) return NULL;
    for (uint32_t i = 0; i < num_disks; i++) vol->disks[i] = disks[i];
    vol->num_disks = num_disks;
    vol->level = 1;
    vol->stripe_size = 0;
    vol->round_robin = 0;

    return raid_alloc_device(vol, "raid1", min_sectors(disks, num_disks));
}

BlockDevice* raid5_create(BlockDevice** disks, uint32_t num_disks, uint64_t stripe_sectors) {
    if (validate_disks(disks, num_disks) != 0) return NULL;
    if (num_disks < 3) return NULL;
    if (stripe_sectors == 0) stripe_sectors = 1;

    RaidVolume* vol = (RaidVolume*)kalloc(sizeof(RaidVolume));
    if (!vol) return NULL;
    for (uint32_t i = 0; i < num_disks; i++) vol->disks[i] = disks[i];
    vol->num_disks = num_disks;
    vol->level = 5;
    vol->stripe_size = stripe_sectors;
    vol->round_robin = 0;

    /* RAID5 usable capacity = (N-1) * smallest_disk */
    uint64_t total = min_sectors(disks, num_disks) * (num_disks - 1) / num_disks;
    uint64_t stripe_group = stripe_sectors * num_disks;
    total = (total / stripe_group) * stripe_group;

    return raid_alloc_device(vol, "raid5", total);
}

BlockDevice* raid10_create(BlockDevice** disks, uint32_t num_disks, uint64_t stripe_sectors) {
    if (validate_disks(disks, num_disks) != 0) return NULL;
    if (num_disks < 4 || (num_disks & 1)) return NULL;
    if (stripe_sectors == 0) stripe_sectors = 1;

    RaidVolume* vol = (RaidVolume*)kalloc(sizeof(RaidVolume));
    if (!vol) return NULL;
    for (uint32_t i = 0; i < num_disks; i++) vol->disks[i] = disks[i];
    vol->num_disks = num_disks;
    vol->level = 10;
    vol->stripe_size = stripe_sectors;
    vol->round_robin = 0;

    uint32_t mirrors = num_disks / 2;
    uint64_t m = min_sectors(disks, num_disks);
    uint64_t stripe_group = stripe_sectors * mirrors;
    uint64_t total = (m / stripe_group) * stripe_group;

    return raid_alloc_device(vol, "raid10", total);
}

void raid_destroy(BlockDevice* dev) {
    if (!dev) return;
    RaidVolume* vol = (RaidVolume*)dev->driver_data;
    if (vol) kfree(vol);
    kfree(dev);
}

void raid_init(void) {
    kprint_timed("Software RAID available (RAID0/RAID1/RAID5/RAID10)\n");
}
