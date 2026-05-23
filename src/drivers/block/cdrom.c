#include "cdrom.h"
#include "freelib/kstdio.h"
#include "freelib/kalloc.h"
#include "drivers/block/ide.h"

static cdrom_drive_t cdrom_drives[4];

void cdrom_init(void) {
    kprint("Initializing CD-ROM driver...\n");
    
    for (int i = 0; i < 4; i++) {
        cdrom_drives[i].present = 0;
        cdrom_drives[i].type = 0;
        cdrom_drives[i].lba = 0;
        cdrom_drives[i].sectors = 0;
    }
    
    kprint("CD-ROM driver initialized\n");
}

int cdrom_read(uint8_t drive, uint32_t lba, uint8_t *buffer, uint32_t sectors) {
    if (drive >= 4 || !cdrom_drives[drive].present)
        return -1;
    if (sectors == 0) return 0;
    (void)lba;
    (void)buffer;
    return -1;
}

cdrom_drive_t *cdrom_get_drive(uint8_t drive) {
    if (drive >= 4) return NULL;
    return &cdrom_drives[drive];
}
