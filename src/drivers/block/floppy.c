#include "floppy.h"
#include "freelib/kstdio.h"
#include "freelib/kalloc.h"

static floppy_drive_t floppy_drives[2];

void floppy_init(void) {
    kprint("Initializing floppy driver...\n");
    
    for (int i = 0; i < 2; i++) {
        floppy_drives[i].present = 0;
        floppy_drives[i].type = 0;
        floppy_drives[i].cylinders = 0;
        floppy_drives[i].heads = 0;
        floppy_drives[i].sectors_per_track = 0;
    }
    
    floppy_drives[0].present = 1;
    floppy_drives[0].type = 1;
    floppy_drives[0].cylinders = 80;
    floppy_drives[0].heads = 2;
    floppy_drives[0].sectors_per_track = 18;
    
    kprint("Floppy driver initialized\n");
}

int floppy_read(uint8_t drive, uint32_t lba, uint8_t *buffer) {
    if (drive >= 2 || !floppy_drives[drive].present)
        return -1;
    floppy_drive_t *drv = &floppy_drives[drive];
    uint32_t sectors_per_cylinder = drv->heads * drv->sectors_per_track;
    if (sectors_per_cylinder == 0 || lba >= sectors_per_cylinder * drv->cylinders)
        return -1;
    return -1;
}

int floppy_write(uint8_t drive, uint32_t lba, uint8_t *buffer) {
    if (drive >= 2 || !floppy_drives[drive].present)
        return -1;
    floppy_drive_t *drv = &floppy_drives[drive];
    uint32_t sectors_per_cylinder = drv->heads * drv->sectors_per_track;
    if (sectors_per_cylinder == 0 || lba >= sectors_per_cylinder * drv->cylinders)
        return -1;
    return -1;
}

floppy_drive_t *floppy_get_drive(uint8_t drive) {
    if (drive >= 2) return NULL;
    return &floppy_drives[drive];
}
