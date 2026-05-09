#ifndef CDROM_H
#define CDROM_H

#include "freelib/kstdint.h"

typedef struct {
    uint8_t present;
    uint8_t type;  // 0 = none, 1 = IDE
    uint32_t lba;
    uint32_t sectors;
} cdrom_drive_t;

void cdrom_init(void);
int cdrom_read(uint8_t drive, uint32_t lba, uint8_t *buffer, uint32_t sectors);
cdrom_drive_t *cdrom_get_drive(uint8_t drive);

#endif
