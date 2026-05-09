#ifndef ISO9660_H
#define ISO9660_H

#include "freelib/kstdint.h"

#define ISO9660_SECTOR_SIZE 2048

typedef struct {
    uint8_t type;
    uint8_t loc[3];
    uint8_t data_len[2];
    uint8_t ext_len[2];
    uint8_t unused[28];
    char system_id[32];
    char volume_id[32];
    uint8_t unused2[8];
    uint32_t volume_space_size;
    uint8_t unused3[32];
    uint16_t volume_set_size;
    uint16_t volume_seq_num;
    uint16_t block_size;
    uint32_t path_table_size;
    uint8_t unused4[4];
    uint32_t path_table_loc;
    uint8_t unused5[12];
    uint32_t root_dir_loc;
    uint8_t root_dir_len[2];
} __attribute__((packed)) iso9660_pvd_t;

typedef struct {
    uint8_t len;
    uint8_t ext_len;
    uint32_t lba;
    uint32_t parent_lba;
    uint8_t flags;
    uint8_t file_unit_size;
    uint8_t gap_size;
    uint32_t volume_seq_num;
    uint8_t name_len;
    char name[1];
} __attribute__((packed)) iso9660_dir_record_t;

int iso9660_init(void);
int iso9660_read(const char *path, uint8_t *buffer, uint32_t len);
int iso9660_stat(const char *path, uint32_t *size, int *is_dir);

#endif
