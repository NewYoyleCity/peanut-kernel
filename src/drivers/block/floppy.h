#ifndef FLOPPY_H
#define FLOPPY_H

#include "freelib/kstdint.h"

typedef struct {
    uint8_t present;
    uint8_t type;  // 0 = none, 1 = 1.44MB 3.5-inch
    uint32_t cylinders;
    uint32_t heads;
    uint32_t sectors_per_track;
} floppy_drive_t;

void floppy_init(void);
int floppy_read(uint8_t drive, uint32_t lba, uint8_t *buffer);
int floppy_write(uint8_t drive, uint32_t lba, uint8_t *buffer);
floppy_drive_t *floppy_get_drive(uint8_t drive);

#endif
