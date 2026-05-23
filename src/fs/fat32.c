/* fat32.c -- FAT12/16/32 filesystem driver.
 *
 * Supports reading and (conditionally) writing files on FAT volumes.
 * Handles Long File Name (LFN) entries, cluster chains, and the
 * three FAT variants.  All path lookups are case-insensitive.
 */

#include "fs/fat32.h"
#include "config.h"

#define FAT32_ATTR_LONG_NAME 0x0F
#define FAT12_EOC 0x0FF8
#define FAT16_EOC 0xFFF8
#define FAT32_EOC 0x0FFFFFF8


/* le16 -- read little-endian 16-bit integer.
 */static uint16_t le16(const uint8_t* p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}


/* le32 -- read little-endian 32-bit integer.
 */static uint32_t le32(const uint8_t* p) {
    return (uint32_t)p[0] |
        ((uint32_t)p[1] << 8) |
        ((uint32_t)p[2] << 16) |
        ((uint32_t)p[3] << 24);
}


/* streq -- exact string comparison.
 */static int streq(const char* a, const char* b) {
    uint32_t i = 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) return 0;
        i++;
    }
    return a[i] == b[i];
}


/* upper_ascii -- convert lowercase ASCII to uppercase.
 */static char upper_ascii(char c) {
    return (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
}


/* streq_ci -- case-insensitive string comparison.
 */static int streq_ci(const char* a, const char* b) {
    uint32_t i = 0;
    while (a[i] && b[i]) {
        if (upper_ascii(a[i]) != upper_ascii(b[i])) return 0;
        i++;
    }
    return a[i] == b[i];
}


/* copy_name -- bounded string copy for directory entry names.
 */static void copy_name(char* dst, const char* src, uint32_t cap) {
    uint32_t i = 0;
    if (cap == 0) return;
    while (i + 1 < cap && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}


/* cluster_lba -- compute the first LBA of a FAT cluster.
 */static uint32_t cluster_lba(Fat32Volume* volume, uint32_t cluster) {
    return volume->data_start_lba +
        (cluster - 2) * volume->sectors_per_cluster;
}


/* read_cluster -- read a full cluster into buffer.
 */static int read_cluster(Fat32Volume* volume, uint32_t cluster, uint8_t* buffer) {
    
    return block_read(volume->partition.disk,
        cluster_lba(volume, cluster),
        volume->sectors_per_cluster,
        buffer);
}


/* next_cluster -- follow the FAT to the next cluster in the chain.
 */static uint32_t next_cluster(Fat32Volume* volume, uint32_t cluster) {
    uint8_t sector[BLOCK_SECTOR_SIZE];
    uint32_t fat_offset;
    if (volume->fat_type == PEANUT_FAT12)
        fat_offset = cluster + (cluster / 2);
    else if (volume->fat_type == PEANUT_FAT16)
        fat_offset = cluster * 2;
    else
        fat_offset = cluster * 4;
    uint32_t sector_index = fat_offset / BLOCK_SECTOR_SIZE;
    uint32_t entry_offset = fat_offset % BLOCK_SECTOR_SIZE;

    if (block_read(volume->partition.disk,
        volume->fat_start_lba + sector_index,
        1,
        sector) != 0) {
        return FAT32_EOC;
    }

    if (volume->fat_type == PEANUT_FAT12) {
        uint16_t val;
        if (entry_offset == BLOCK_SECTOR_SIZE - 1) {
            uint8_t next[BLOCK_SECTOR_SIZE];
            if (block_read(volume->partition.disk, volume->fat_start_lba + sector_index + 1, 1, next) != 0)
                return FAT12_EOC;
            val = (uint16_t)sector[entry_offset] | ((uint16_t)next[0] << 8);
        } else {
            val = le16(sector + entry_offset);
        }
        if (cluster & 1)
            val >>= 4;
        else
            val &= 0x0FFF;
        return val >= FAT12_EOC ? FAT32_EOC : val;
    }
    if (volume->fat_type == PEANUT_FAT16) {
        uint16_t val = le16(sector + entry_offset);
        return val >= FAT16_EOC ? FAT32_EOC : val;
    }
    return (le32(sector + entry_offset) & 0x0FFFFFFF);
}


/* write_cluster -- write a full cluster from buffer to disk.
 */static int write_cluster(Fat32Volume* volume, uint32_t cluster, const uint8_t* buffer) {
    
    return block_write(volume->partition.disk,
        cluster_lba(volume, cluster),
        volume->sectors_per_cluster,
        buffer);
}


/* make_short_name -- decode an 8.3 short filename from raw directory entry.
 */static void make_short_name(const uint8_t* raw, char* out) {
    uint32_t out_i = 0;

    for (uint32_t i = 0; i < 8 && raw[i] != ' '; i++) {
        out[out_i++] = (char)raw[i];
    }

    if (raw[8] != ' ') {
        out[out_i++] = '.';
        for (uint32_t i = 8; i < 11 && raw[i] != ' '; i++) {
            out[out_i++] = (char)raw[i];
        }
    }

    out[out_i] = '\0';
}


/* parse_entry -- parse a single FAT directory entry (skip LFN and deleted).
 */static int parse_entry(const uint8_t* raw, Fat32DirEntry* out) {
    if (raw[0] == 0x00) return 0;
    if (raw[0] == 0xE5) return -1;
    if ((raw[11] & FAT32_ATTR_LONG_NAME) == FAT32_ATTR_LONG_NAME) return -1;

    make_short_name(raw, out->name);
    out->attributes = raw[11];
    out->first_cluster = ((uint32_t)le16(raw + 20) << 16) | le16(raw + 26);
    out->size = le32(raw + 28);
    return 1;
}


/* clear_lfn -- reset the Long File Name accumulator.
 */static void clear_lfn(char* lfn) {
    lfn[0] = '\0';
}


/* parse_lfn_entry -- decode one LFN entry and append to accumulator.
 */static void parse_lfn_entry(const uint8_t* raw, char* lfn, uint32_t lfn_size) {
    static const uint8_t offsets[13] = {
        1, 3, 5, 7, 9,
        14, 16, 18, 20, 22, 24,
        28, 30
    };
    uint8_t seq = raw[0] & 0x1F;
    uint32_t base;

    if (lfn_size == 0 || seq == 0) return;
    if (raw[0] & 0x40) clear_lfn(lfn);

    base = (uint32_t)(seq - 1u) * 13u;
    for (uint32_t i = 0; i < 13 && base + i + 1 < lfn_size; i++) {
        uint16_t ch = le16(raw + offsets[i]);
        if (ch == 0x0000 || ch == 0xFFFF) {
            lfn[base + i] = '\0';
            return;
        }
        lfn[base + i] = ch < 128 ? (char)ch : '?';
        lfn[base + i + 1] = '\0';
    }
}

int fat32_mount(Fat32Volume* volume, const Partition* partition) {
    uint8_t sector[BLOCK_SECTOR_SIZE];

    if (!volume || !partition || !partition_is_fat(partition)) return -1;
    if (block_read(partition->disk, partition->first_lba, 1, sector) != 0) return -1;
    if (sector[510] != 0x55 || sector[511] != 0xAA) return -1;
    if (le16(sector + 11) != BLOCK_SECTOR_SIZE) return -1;

    uint8_t sectors_per_cluster = sector[13];
    uint16_t reserved_sectors = le16(sector + 14);
    uint8_t fat_count = sector[16];
    uint16_t root_entries = le16(sector + 17);
    uint16_t total16 = le16(sector + 19);
    uint32_t total32 = le32(sector + 32);
    uint16_t spf16 = le16(sector + 22);
    uint32_t spf32 = le32(sector + 36);
    uint32_t sectors_per_fat = spf16 ? spf16 : spf32;
    uint32_t total_sectors = total16 ? total16 : total32;
    uint32_t root_dir_sectors = ((uint32_t)root_entries * 32u + BLOCK_SECTOR_SIZE - 1u) / BLOCK_SECTOR_SIZE;
    uint32_t root_cluster = root_entries ? 0 : le32(sector + 44);

    if (sectors_per_cluster == 0 || fat_count == 0 || sectors_per_fat == 0 || total_sectors == 0) {
        return -1;
    }
    uint32_t data_sectors = total_sectors - (reserved_sectors + (uint32_t)fat_count * sectors_per_fat + root_dir_sectors);
    uint32_t cluster_count = data_sectors / sectors_per_cluster;
    uint8_t fat_type;
    if (cluster_count < 4085)
        fat_type = PEANUT_FAT12;
    else if (cluster_count < 65525)
        fat_type = PEANUT_FAT16;
    else
        fat_type = PEANUT_FAT32;

#if !defined(CONFIG_FS_FAT12_READ)
    if (fat_type == PEANUT_FAT12) return -1;
#endif
#if !defined(CONFIG_FS_FAT16_READ)
    if (fat_type == PEANUT_FAT16) return -1;
#endif
#if !defined(CONFIG_FS_FAT32_READ)
    if (fat_type == PEANUT_FAT32) return -1;
#endif
    if (fat_type == PEANUT_FAT32 && root_cluster < 2)
        return -1;

    volume->partition = *partition;
    volume->fat_type = fat_type;
    volume->fat_start_lba = partition->first_lba + reserved_sectors;
    volume->sectors_per_fat = sectors_per_fat;
    volume->root_cluster = root_cluster;
    volume->sectors_per_cluster = sectors_per_cluster;
    volume->root_entry_count = root_entries;
    volume->root_dir_lba = volume->fat_start_lba + (fat_count * sectors_per_fat);
    volume->root_dir_sectors = root_dir_sectors;
    volume->data_start_lba = volume->root_dir_lba + root_dir_sectors;

    return 0;
}

uint32_t fat32_list_root(Fat32Volume* volume, Fat32DirEntry* entries, uint32_t max_entries) {
    uint8_t cluster_buffer[BLOCK_SECTOR_SIZE * 8];
    uint32_t cluster = volume->root_cluster;
    uint32_t count = 0;

    if (!volume || !entries || max_entries == 0 || volume->sectors_per_cluster > 8) return 0;

    if (volume->fat_type != PEANUT_FAT32) {
        uint8_t sector[BLOCK_SECTOR_SIZE];
        char lfn[FAT32_MAX_NAME];
        clear_lfn(lfn);
        for (uint32_t s = 0; s < volume->root_dir_sectors && count < max_entries; s++) {
            if (block_read(volume->partition.disk, volume->root_dir_lba + s, 1, sector) != 0)
                break;
            for (uint32_t off = 0; off < BLOCK_SECTOR_SIZE && count < max_entries; off += 32) {
                Fat32DirEntry parsed;
                if ((sector[off + 11] & FAT32_ATTR_LONG_NAME) == FAT32_ATTR_LONG_NAME) {
                    parse_lfn_entry(sector + off, lfn, sizeof(lfn));
                    continue;
                }
                int result = parse_entry(sector + off, &parsed);
                if (result == 0) return count;
                if (result < 0) {
                    clear_lfn(lfn);
                    continue;
                }
                if (lfn[0]) {
                    copy_name(parsed.name, lfn, sizeof(parsed.name));
                    clear_lfn(lfn);
                }
                entries[count++] = parsed;
            }
        }
        return count;
    }

    while (cluster < FAT32_EOC && count < max_entries) {
        char lfn[FAT32_MAX_NAME];
        clear_lfn(lfn);
        if (read_cluster(volume, cluster, cluster_buffer) != 0) break;

        uint32_t bytes = volume->sectors_per_cluster * BLOCK_SECTOR_SIZE;
        for (uint32_t off = 0; off < bytes && count < max_entries; off += 32) {
            Fat32DirEntry parsed;
            if ((cluster_buffer[off + 11] & FAT32_ATTR_LONG_NAME) == FAT32_ATTR_LONG_NAME) {
                parse_lfn_entry(cluster_buffer + off, lfn, sizeof(lfn));
                continue;
            }
            int result = parse_entry(cluster_buffer + off, &parsed);
            if (result == 0) return count;
            if (result < 0) {
                clear_lfn(lfn);
                continue;
            }
            if (lfn[0]) {
                copy_name(parsed.name, lfn, sizeof(parsed.name));
                clear_lfn(lfn);
            }
            entries[count++] = parsed;
        }

        cluster = next_cluster(volume, cluster);
    }

    return count;
}

int fat32_find_dir_in_root(Fat32Volume* volume, const char* dirname, Fat32DirEntry* out) {
    Fat32DirEntry entries[64];
    uint32_t entry_count = fat32_list_root(volume, entries, 64);

    if (!volume || !dirname || !out) return -1;

    for (uint32_t i = 0; i < entry_count; i++) {
        if ((entries[i].attributes & FAT32_ATTR_DIRECTORY) && streq_ci(entries[i].name, dirname)) {
            *out = entries[i];
            return 0;
        }
    }
    return -1;
}


/* list_cluster_entries -- enumerate entries in a directory cluster chain.
 */static uint32_t list_cluster_entries(Fat32Volume* volume, uint32_t cluster, Fat32DirEntry* entries, uint32_t max_entries) {
    uint8_t cluster_buffer[BLOCK_SECTOR_SIZE * 8];
    uint32_t count = 0;

    if (volume->sectors_per_cluster > 8) return 0;

    while (cluster < FAT32_EOC && count < max_entries) {
        char lfn[FAT32_MAX_NAME];
        clear_lfn(lfn);
        if (read_cluster(volume, cluster, cluster_buffer) != 0) break;

        uint32_t bytes = volume->sectors_per_cluster * BLOCK_SECTOR_SIZE;
        for (uint32_t off = 0; off < bytes && count < max_entries; off += 32) {
            Fat32DirEntry parsed;
            if ((cluster_buffer[off + 11] & FAT32_ATTR_LONG_NAME) == FAT32_ATTR_LONG_NAME) {
                parse_lfn_entry(cluster_buffer + off, lfn, sizeof(lfn));
                continue;
            }
            int result = parse_entry(cluster_buffer + off, &parsed);
            if (result == 0) return count;
            if (result < 0) {
                clear_lfn(lfn);
                continue;
            }
            if (lfn[0]) {
                copy_name(parsed.name, lfn, sizeof(parsed.name));
                clear_lfn(lfn);
            }
            entries[count++] = parsed;
        }
        cluster = next_cluster(volume, cluster);
    }
    return count;
}

int fat32_find_in_dir(Fat32Volume* volume, uint32_t dir_cluster, const char* name, Fat32DirEntry* out) {
    Fat32DirEntry entries[64];
    uint32_t entry_count = list_cluster_entries(volume, dir_cluster, entries, 64);

    for (uint32_t i = 0; i < entry_count; i++) {
        if (streq_ci(entries[i].name, name)) {
            *out = entries[i];
            return 0;
        }
    }
    return -1;
}

int fat32_find_root(Fat32Volume* volume, const char* path, Fat32DirEntry* out) {
    if (!volume || !path || !out) return -1;

    const char* p = path;
    uint32_t dir_cluster = volume->root_cluster;
    int in_root = 1;
    Fat32DirEntry current;

    while (*p == '/' || *p == '\\') p++;
    while (*p) {
        char component[FAT32_MAX_NAME];
        uint32_t len = 0;

        while (p[len] && p[len] != '/' && p[len] != '\\') {
            if (len + 1 >= sizeof(component)) return -1;
            component[len] = p[len];
            len++;
        }
        component[len] = '\0';
        while (p[len] == '/' || p[len] == '\\') len++;

        if (in_root) {
            Fat32DirEntry entries[64];
            uint32_t entry_count = fat32_list_root(volume, entries, 64);
            int found = 0;
            for (uint32_t i = 0; i < entry_count; i++) {
                if (streq_ci(entries[i].name, component)) {
                    current = entries[i];
                    found = 1;
                    break;
                }
            }
            if (!found) return -1;
        } else if (fat32_find_in_dir(volume, dir_cluster, component, &current) != 0) {
            return -1;
        }

        p += len;
        if (!*p) {
            *out = current;
            return 0;
        }
        if ((current.attributes & FAT32_ATTR_DIRECTORY) == 0) return -1;
        dir_cluster = current.first_cluster;
        in_root = 0;
    }

    return -1;
}

int fat32_read_file(Fat32Volume* volume, const char* name, void* buffer, uint32_t buffer_size, uint32_t* bytes_read) {
    Fat32DirEntry file_storage;
    Fat32DirEntry* file = &file_storage;
    uint8_t cluster_buffer[BLOCK_SECTOR_SIZE * 8];
    uint8_t* out = (uint8_t*)buffer;
    uint32_t copied = 0;

    if (!volume || !name || !buffer || !bytes_read) return -1;
    if (fat32_find_root(volume, name, file) != 0) return -1;
    if (file->attributes & FAT32_ATTR_DIRECTORY) return -1;
    if (file->size > buffer_size || volume->sectors_per_cluster > 8) return -1;

    uint32_t cluster = file->first_cluster;
    while (cluster < FAT32_EOC && copied < file->size) {
        if (read_cluster(volume, cluster, cluster_buffer) != 0) return -1;

        uint32_t cluster_bytes = volume->sectors_per_cluster * BLOCK_SECTOR_SIZE;
        uint32_t left = file->size - copied;
        uint32_t to_copy = left < cluster_bytes ? left : cluster_bytes;
        for (uint32_t i = 0; i < to_copy; i++) out[copied + i] = cluster_buffer[i];
        copied += to_copy;
        cluster = next_cluster(volume, cluster);
    }

    *bytes_read = copied;
    return 0;
}

int fat32_write_file(Fat32Volume* volume, const char* name, const void* buffer, uint32_t size) {
    Fat32DirEntry file;
    uint8_t cluster_buffer[BLOCK_SECTOR_SIZE * 8];
    const uint8_t* in = (const uint8_t*)buffer;
    uint32_t copied = 0;

    if (!volume || !name || !buffer) return -1;
#if !defined(CONFIG_FS_FAT12_WRITE)
    if (volume->fat_type == PEANUT_FAT12) return -1;
#endif
#if !defined(CONFIG_FS_FAT16_WRITE)
    if (volume->fat_type == PEANUT_FAT16) return -1;
#endif
#if !defined(CONFIG_FS_FAT32_WRITE)
    if (volume->fat_type == PEANUT_FAT32) return -1;
#endif
    if (fat32_find_root(volume, name, &file) != 0) return -1;
    if (file.attributes & FAT32_ATTR_DIRECTORY) return -1;
    if (size > file.size || volume->sectors_per_cluster > 8) return -1;

    uint32_t cluster = file.first_cluster;
    while (cluster < FAT32_EOC && copied < size) {
        if (read_cluster(volume, cluster, cluster_buffer) != 0) return -1;

        uint32_t cluster_bytes = volume->sectors_per_cluster * BLOCK_SECTOR_SIZE;
        uint32_t left = size - copied;
        uint32_t to_copy = left < cluster_bytes ? left : cluster_bytes;
        for (uint32_t i = 0; i < to_copy; i++) cluster_buffer[i] = in[copied + i];

        if (write_cluster(volume, cluster, cluster_buffer) != 0) return -1;
        copied += to_copy;
        cluster = next_cluster(volume, cluster);
    }

    return copied == size ? 0 : -1;
}
