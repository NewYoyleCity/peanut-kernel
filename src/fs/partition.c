/* partition.c -- MBR partition table scanner.
 *
 * Reads sector 0, validates the 0x55AA signature, and extracts up to
 * PARTITION_MAX entries.  Provides helpers to query partition type.
 */

#include "fs/partition.h"


/* le32 -- read little-endian 32-bit integer from byte buffer.
 */static uint32_t le32(const uint8_t* p) {
    return (uint32_t)p[0] |
        ((uint32_t)p[1] << 8) |
        ((uint32_t)p[2] << 16) |
        ((uint32_t)p[3] << 24);
}


/* partition_is_fat32 -- return 1 if the partition type is FAT32 (0x0B, 0x0C, 0x1B, 0x1C).
 */
int partition_is_fat32(const Partition* partition) {
    if (!partition) return 0;
    return partition->type == 0x0B ||
        partition->type == 0x0C ||
        partition->type == 0x1B ||
        partition->type == 0x1C;
}


/* partition_is_fat -- return 1 for any FAT partition type.
 */
int partition_is_fat(const Partition* partition) {
    if (!partition) return 0;
    if (partition_is_fat32(partition)) return 1;
    return partition->type == 0x01 ||
        partition->type == 0x04 ||
        partition->type == 0x06 ||
        partition->type == 0x0E ||
        partition->type == 0x11 ||
        partition->type == 0x14 ||
        partition->type == 0x16 ||
        partition->type == 0x1E;
}


/* partition_scan_mbr -- read the MBR and extract partition entries.
 */
int partition_scan_mbr(BlockDevice* disk, Partition* out, uint32_t max_partitions) {
    uint8_t sector[BLOCK_SECTOR_SIZE];
    uint32_t found = 0;

    if (!disk || !out || max_partitions == 0) return -1;
    if (block_read(disk, 0, 1, sector) != 0) return -1;
    if (sector[510] != 0x55 || sector[511] != 0xAA) return 0;

    for (uint32_t i = 0; i < PARTITION_MAX && found < max_partitions; i++) {
        uint8_t* entry = sector + 446 + i * 16;
        uint8_t type = entry[4];
        uint32_t first_lba = le32(entry + 8);
        uint32_t sectors = le32(entry + 12);

        if (type == 0 || sectors == 0) continue;

        out[found].disk = disk;
        out[found].type = type;
        out[found].first_lba = first_lba;
        out[found].sector_count = sectors;
        found++;
    }

    return (int)found;
}
