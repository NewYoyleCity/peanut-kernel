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
    
    /*
     * The built-in profile describes the standard 1.44 MiB geometry used by
     * FAT12 boot media. Controller I/O is kept separate from geometry so
     * callers can validate addresses even when no FDC backend is present.
     */
    floppy_drives[0].present = 1;
    floppy_drives[0].type = 1;
    floppy_drives[0].cylinders = 80;
    floppy_drives[0].heads = 2;
    floppy_drives[0].sectors_per_track = 18;
    
    kprint("Floppy driver initialized\n");
}

int floppy_read(uint8_t drive, uint32_t lba, uint8_t *buffer) {
    if (drive >= 2 || !floppy_drives[drive].present) {
        return -1;
    }
    
    floppy_drive_t *drv = &floppy_drives[drive];
    
    /* Convert the logical sector number into the CHS tuple used by the FDC. */
    uint32_t sectors_per_cylinder = drv->heads * drv->sectors_per_track;
    if (sectors_per_cylinder == 0 || lba >= sectors_per_cylinder * drv->cylinders) {
        return -1;
    }

    uint32_t cylinder = lba / sectors_per_cylinder;
    uint32_t temp = lba % sectors_per_cylinder;
    uint32_t head = temp / drv->sectors_per_track;
    uint32_t sector = temp % drv->sectors_per_track + 1;
    
    (void)cylinder;
    (void)head;
    (void)sector;
    (void)buffer;
    
    return -1;
}

int floppy_write(uint8_t drive, uint32_t lba, uint8_t *buffer) {
    if (drive >= 2 || !floppy_drives[drive].present) {
        return -1;
    }
    
    floppy_drive_t *drv = &floppy_drives[drive];
    uint32_t sectors_per_cylinder = drv->heads * drv->sectors_per_track;
    if (sectors_per_cylinder == 0 || lba >= sectors_per_cylinder * drv->cylinders) {
        return -1;
    }

    (void)buffer;
    
    return -1;
}

floppy_drive_t *floppy_get_drive(uint8_t drive) {
    if (drive >= 2) {
        return NULL;
    }
    return &floppy_drives[drive];
}
