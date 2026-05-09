#include "fat12.h"
#include "freelib/kstdio.h"
#include "freelib/kalloc.h"
#include "drivers/block/floppy.h"

static fat12_bpb_t *fat12_bpb = NULL;

#define FAT12_ATTR_DIRECTORY 0x10
#define FAT12_EOC_MARK 0x0FF8

static uint16_t fat12_root_dir_sectors(void) {
    return (uint16_t)(((uint32_t)fat12_bpb->root_entries * 32u + fat12_bpb->bytes_per_sector - 1u) /
        fat12_bpb->bytes_per_sector);
}

static uint32_t fat12_root_lba(void) {
    return fat12_bpb->reserved_sectors + (uint32_t)fat12_bpb->num_fats * fat12_bpb->sectors_per_fat_small;
}

static uint32_t fat12_data_lba(void) {
    return fat12_root_lba() + fat12_root_dir_sectors();
}

static uint32_t fat12_cluster_lba(uint16_t cluster) {
    return fat12_data_lba() + (uint32_t)(cluster - 2u) * fat12_bpb->sectors_per_cluster;
}

static char upper_ascii(char c) {
    if (c >= 'a' && c <= 'z') return (char)(c - ('a' - 'A'));
    return c;
}

static int fat12_make_name(const char *path, char out[11]) {
    uint32_t i = 0;
    uint32_t base = 0;
    uint32_t ext = 8;
    int in_ext = 0;

    for (uint32_t j = 0; j < 11; j++) out[j] = ' ';
    while (path[i] == '/' || path[i] == '\\') i++;

    for (; path[i]; i++) {
        char c = path[i];
        if (c == '/' || c == '\\') return -1;
        if (c == '.') {
            if (in_ext) return -1;
            in_ext = 1;
            continue;
        }
        if (!in_ext) {
            if (base >= 8) return -1;
            out[base++] = upper_ascii(c);
        } else {
            if (ext >= 11) return -1;
            out[ext++] = upper_ascii(c);
        }
    }

    return base == 0 ? -1 : 0;
}

static int fat12_name_matches(const fat12_dirent_t *entry, const char name[11]) {
    for (uint32_t i = 0; i < 8; i++) {
        if (entry->name[i] != name[i]) return 0;
    }
    for (uint32_t i = 0; i < 3; i++) {
        if (entry->ext[i] != name[8 + i]) return 0;
    }
    return 1;
}

static uint16_t fat12_next_cluster(uint16_t cluster) {
    uint8_t sector[512];
    uint32_t fat_offset = cluster + (cluster / 2u);
    uint32_t sector_lba = fat12_bpb->reserved_sectors + fat_offset / fat12_bpb->bytes_per_sector;
    uint32_t offset = fat_offset % fat12_bpb->bytes_per_sector;
    uint16_t value;

    if (floppy_read(0, sector_lba, sector) != 0) return FAT12_EOC_MARK;
    if (offset == fat12_bpb->bytes_per_sector - 1u) {
        uint8_t next_sector[512];
        if (floppy_read(0, sector_lba + 1u, next_sector) != 0) return FAT12_EOC_MARK;
        value = (uint16_t)sector[offset] | ((uint16_t)next_sector[0] << 8);
    } else {
        value = (uint16_t)sector[offset] | ((uint16_t)sector[offset + 1u] << 8);
    }

    if (cluster & 1u) value >>= 4;
    else value &= 0x0FFF;
    return value;
}

static int fat12_find_root(const char *path, fat12_dirent_t *out) {
    char wanted[11];
    uint8_t sector[512];
    uint32_t root_lba;
    uint32_t root_sectors;

    if (!fat12_bpb || !path || !out) return -1;
    if (fat12_make_name(path, wanted) != 0) return -1;

    root_lba = fat12_root_lba();
    root_sectors = fat12_root_dir_sectors();
    for (uint32_t s = 0; s < root_sectors; s++) {
        if (floppy_read(0, root_lba + s, sector) != 0) return -1;
        for (uint32_t off = 0; off < fat12_bpb->bytes_per_sector; off += sizeof(fat12_dirent_t)) {
            fat12_dirent_t *entry = (fat12_dirent_t *)(sector + off);
            if ((uint8_t)entry->name[0] == 0x00) return -1;
            if ((uint8_t)entry->name[0] == 0xE5) continue;
            if ((entry->attributes & 0x0F) == 0x0F) continue;
            if (fat12_name_matches(entry, wanted)) {
                *out = *entry;
                return 0;
            }
        }
    }
    return -1;
}

int fat12_init(void) {
    kprint("Initializing FAT12 filesystem...\n");
    
    // Read boot sector from floppy
    uint8_t *sector_buffer = kalloc(512);
    if (!sector_buffer) {
        return -1;
    }
    
    int ret = floppy_read(0, 0, sector_buffer);
    if (ret != 0) {
        kfree(sector_buffer);
        return -1;
    }
    
    fat12_bpb = (fat12_bpb_t *)sector_buffer;
    
    // Validate BPB
    if (fat12_bpb->bytes_per_sector != 512) {
        kfree(sector_buffer);
        return -1;
    }
    
    kprint("FAT12 filesystem initialized\n");
    return 0;
}

int fat12_read(const char *path, uint8_t *buffer, uint32_t len) {
    fat12_dirent_t file;
    uint8_t sector[512];
    uint32_t copied = 0;
    uint16_t cluster;

    if (!buffer || fat12_find_root(path, &file) != 0) return -1;
    if (file.attributes & FAT12_ATTR_DIRECTORY) return -1;
    if (len < file.file_size) return -1;

    cluster = file.first_cluster_low;
    while (cluster >= 2 && cluster < FAT12_EOC_MARK && copied < file.file_size) {
        uint32_t cluster_lba = fat12_cluster_lba(cluster);
        for (uint32_t s = 0; s < fat12_bpb->sectors_per_cluster && copied < file.file_size; s++) {
            if (floppy_read(0, cluster_lba + s, sector) != 0) return -1;
            uint32_t left = file.file_size - copied;
            uint32_t count = left < fat12_bpb->bytes_per_sector ? left : fat12_bpb->bytes_per_sector;
            for (uint32_t i = 0; i < count; i++) buffer[copied + i] = sector[i];
            copied += count;
        }
        cluster = fat12_next_cluster(cluster);
    }

    return copied == file.file_size ? (int)copied : -1;
}

int fat12_stat(const char *path, uint32_t *size, int *is_dir) {
    fat12_dirent_t file;

    if (fat12_find_root(path, &file) != 0) return -1;
    if (size) *size = file.file_size;
    if (is_dir) *is_dir = (file.attributes & FAT12_ATTR_DIRECTORY) ? 1 : 0;
    return 0;
}
