#ifndef FAT12_H
#define FAT12_H

#include "freelib/kstdint.h"

typedef struct {
    uint8_t jmp[3];
    char oem[8];
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t num_fats;
    uint16_t root_entries;
    uint16_t total_sectors_small;
    uint8_t media_descriptor;
    uint16_t sectors_per_fat_small;
    uint16_t sectors_per_track;
    uint16_t num_heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_large;
} __attribute__((packed)) fat12_bpb_t;

typedef struct {
    char name[8];
    char ext[3];
    uint8_t attributes;
    uint8_t reserved;
    uint8_t create_time_tenth;
    uint16_t create_time;
    uint16_t create_date;
    uint16_t last_access_date;
    uint16_t first_cluster_high;
    uint16_t write_time;
    uint16_t write_date;
    uint16_t first_cluster_low;
    uint32_t file_size;
} __attribute__((packed)) fat12_dirent_t;

int fat12_init(void);
int fat12_read(const char *path, uint8_t *buffer, uint32_t len);
int fat12_stat(const char *path, uint32_t *size, int *is_dir);

#endif
