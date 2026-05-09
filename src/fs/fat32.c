#include "fs/fat32.h"
#include "config.h"

#define FAT32_ATTR_LONG_NAME 0x0F
#define FAT12_EOC 0x0FF8
#define FAT16_EOC 0xFFF8
#define FAT32_EOC 0x0FFFFFF8

static uint16_t le16(const uint8_t* p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t le32(const uint8_t* p) {
    return (uint32_t)p[0] |
        ((uint32_t)p[1] << 8) |
        ((uint32_t)p[2] << 16) |
        ((uint32_t)p[3] << 24);
}

static int streq(const char* a, const char* b) {
    uint32_t i = 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) return 0;
        i++;
    }
    return a[i] == b[i];
}

static uint32_t cluster_lba(Fat32Volume* volume, uint32_t cluster) {
    return volume->data_start_lba +
        (cluster - 2) * volume->sectors_per_cluster;
}

static int read_cluster(Fat32Volume* volume, uint32_t cluster, uint8_t* buffer) {
    
    return block_read(volume->partition.disk,
        cluster_lba(volume, cluster),
        volume->sectors_per_cluster,
        buffer);
}

static uint32_t next_cluster(Fat32Volume* volume, uint32_t cluster) {
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

static int write_cluster(Fat32Volume* volume, uint32_t cluster, const uint8_t* buffer) {
    
    return block_write(volume->partition.disk,
        cluster_lba(volume, cluster),
        volume->sectors_per_cluster,
        buffer);
}

static void make_short_name(const uint8_t* raw, char* out) {
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

static int parse_entry(const uint8_t* raw, Fat32DirEntry* out) {
    if (raw[0] == 0x00) return 0;
    if (raw[0] == 0xE5) return -1;
    if ((raw[11] & FAT32_ATTR_LONG_NAME) == FAT32_ATTR_LONG_NAME) return -1;

    make_short_name(raw, out->name);
    out->attributes = raw[11];
    out->first_cluster = ((uint32_t)le16(raw + 20) << 16) | le16(raw + 26);
    out->size = le32(raw + 28);
    return 1;
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
        for (uint32_t s = 0; s < volume->root_dir_sectors && count < max_entries; s++) {
            if (block_read(volume->partition.disk, volume->root_dir_lba + s, 1, sector) != 0)
                break;
            for (uint32_t off = 0; off < BLOCK_SECTOR_SIZE && count < max_entries; off += 32) {
                Fat32DirEntry parsed;
                int result = parse_entry(sector + off, &parsed);
                if (result == 0) return count;
                if (result < 0) continue;
                entries[count++] = parsed;
            }
        }
        return count;
    }

    while (cluster < FAT32_EOC && count < max_entries) {
        if (read_cluster(volume, cluster, cluster_buffer) != 0) break;

        uint32_t bytes = volume->sectors_per_cluster * BLOCK_SECTOR_SIZE;
        for (uint32_t off = 0; off < bytes && count < max_entries; off += 32) {
            Fat32DirEntry parsed;
            int result = parse_entry(cluster_buffer + off, &parsed);
            if (result == 0) return count;
            if (result < 0) continue;
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
        if ((entries[i].attributes & FAT32_ATTR_DIRECTORY) && streq(entries[i].name, dirname)) {
            *out = entries[i];
            return 0;
        }
    }
    return -1;
}

static uint32_t list_cluster_entries(Fat32Volume* volume, uint32_t cluster, Fat32DirEntry* entries, uint32_t max_entries) {
    uint8_t cluster_buffer[BLOCK_SECTOR_SIZE * 8];
    uint32_t count = 0;

    if (volume->sectors_per_cluster > 8) return 0;

    while (cluster < FAT32_EOC && count < max_entries) {
        if (read_cluster(volume, cluster, cluster_buffer) != 0) break;

        uint32_t bytes = volume->sectors_per_cluster * BLOCK_SECTOR_SIZE;
        for (uint32_t off = 0; off < bytes && count < max_entries; off += 32) {
            Fat32DirEntry parsed;
            int result = parse_entry(cluster_buffer + off, &parsed);
            if (result == 0) return count;
            if (result < 0) continue;
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
        if (streq(entries[i].name, name)) {
            *out = entries[i];
            return 0;
        }
    }
    return -1;
}

int fat32_find_root(Fat32Volume* volume, const char* path, Fat32DirEntry* out) {
    if (!volume || !path || !out) return -1;

    const char* slash = 0;
    for (int i = 0; path[i]; i++) {
        if (path[i] == '/' || path[i] == '\\') {
            slash = path + i;
            break;
        }
    }

    if (slash) {
        char dirname[16];
        uint32_t dlen = slash - path;
        if (dlen >= sizeof(dirname)) return -1;
        
        for (uint32_t i = 0; i < dlen; i++) {
            dirname[i] = path[i];
        }
        dirname[dlen] = '\0';
        
        const char* filename = slash + 1;
        
        Fat32DirEntry dir_entry;
        if (fat32_find_dir_in_root(volume, dirname, &dir_entry) != 0) return -1;
        
        return fat32_find_in_dir(volume, dir_entry.first_cluster, filename, out);
    }
    
    Fat32DirEntry entries[64];
    uint32_t entry_count = fat32_list_root(volume, entries, 64);

    for (uint32_t i = 0; i < entry_count; i++) {
        if (streq(entries[i].name, path)) {
            *out = entries[i];
            return 0;
        }
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
