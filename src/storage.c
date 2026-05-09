#include "storage.h"
#include "config.h"
#include "freelib/kstdio.h"
#include "freelib/kalloc.h"
#include "freelib/kpanic.h"
#include "fs/vfs.h"
#include "fs/fat32.h"
#include "fs/exfat.h"
#include "fs/extfs.h"
#include "fs/iso9660.h"
#include "fs/fat12.h"
#include "drivers/block/ide.h"
#include "drivers/block/ahci.h"
#include "drivers/block/cdrom.h"
#include "drivers/block/floppy.h"

#ifdef CONFIG_STORAGE_TESTS
#include "tests/storage_tests.h"
#endif

static PeanutVolume root_volume;
static int root_mounted = 0;

PeanutVolume* storage_get_root_volume() {
    return root_mounted ? &root_volume : NULL;
}

static int fhs_dir_exists_fat32(Fat32Volume* volume, const char* name) {
    Fat32DirEntry entry;
    if (fat32_find_root(volume, name, &entry) != 0)
        return 0;
    return (entry.attributes & FAT32_ATTR_DIRECTORY) != 0;
}

static int fhs_dir_exists_ext(ExtVolume* v, const char* name) {
    char p[16];
    uint32_t i = 0;
    if (i + 1 >= sizeof(p)) return 0;
    p[i++] = '/';
    for (uint32_t j = 0; name[j] && i + 1 < sizeof(p); j++) {
        p[i++] = name[j];
    }
    p[i] = '\0';
    return extfs_dir_exists(v, p);
}

static int validate_fhs(PeanutVolume* pv) {
    if (pv->fs_kind == PEANUT_FS_EXFAT) {
        return exfat_dir_exists(&pv->exfat, "BOOT") &&
               exfat_dir_exists(&pv->exfat, "BIN") &&
               exfat_dir_exists(&pv->exfat, "USR") &&
               exfat_dir_exists(&pv->exfat, "LIB");
    }
    if (pv->fs_kind == PEANUT_FS_EXT) {
        return fhs_dir_exists_ext(&pv->ext, "BOOT") &&
               fhs_dir_exists_ext(&pv->ext, "BIN") &&
               fhs_dir_exists_ext(&pv->ext, "USR") &&
               fhs_dir_exists_ext(&pv->ext, "LIB");
    }
    return fhs_dir_exists_fat32(&pv->fat32, "BOOT") &&
           fhs_dir_exists_fat32(&pv->fat32, "BIN") &&
           fhs_dir_exists_fat32(&pv->fat32, "USR") &&
           fhs_dir_exists_fat32(&pv->fat32, "LIB");
}

static int mount_first_fs(BlockDevice* dev, PeanutVolume* out) {
    Partition partitions[PARTITION_MAX];
    int count = partition_scan_mbr(dev, partitions, PARTITION_MAX);

    if (count <= 0) {
        partitions[0].disk = dev;
        partitions[0].type = 0;
        partitions[0].first_lba = 0;
        partitions[0].sector_count = dev->sector_count;
        count = 1;
    }

    for (int i = 0; i < count; i++) {
        if (partition_is_fat(&partitions[i])) {
            if (fat32_mount(&out->fat32, &partitions[i]) == 0) {
                out->fs_kind = PEANUT_FS_FAT32;
                return 1;
            }
        }
    }

#ifdef CONFIG_FS_EXFAT_READ
    for (int i = 0; i < count; i++) {
        if (exfat_probe_boot(dev, partitions[i].first_lba)) {
            if (exfat_mount(&out->exfat, &partitions[i]) == 0) {
                out->fs_kind = PEANUT_FS_EXFAT;
                return 1;
            }
        }
    }
#endif

#if defined(CONFIG_FS_EXT4_READ) || defined(CONFIG_FS_EXT3_READ) || defined(CONFIG_FS_EXT2_READ)
    for (int i = 0; i < count; i++) {
        if (extfs_probe(dev, partitions[i].first_lba)) {
            if (extfs_mount(&out->ext, &partitions[i]) == 0) {
                out->fs_kind = PEANUT_FS_EXT;
                return 1;
            }
        }
    }
#endif

    return 0;
}

void storage_init_required() {
    kprint("Setting up filesystems on block devices...\n");

    int ide_found = 0;
    int ahci_found = 0;

#ifdef CONFIG_STORAGE_IDE
    ide_found = ide_init();
#endif

#ifdef CONFIG_STORAGE_AHCI
    ahci_found = ahci_init();
#endif

    kprint("SATA drives found: ");
    kprint_int(ahci_found);
    kprint("\n");
    kprint("IDE drives found: ");
    kprint_int(ide_found);
    kprint("\n");

    if (ide_found == 0 && ahci_found == 0) {
        kpanic("[Peanut kernel - panic - no drives found!]");
    }

#ifdef CONFIG_STORAGE_AHCI
    for (uint32_t i = 0; i < ahci_device_count(); i++) {
        if (mount_first_fs(ahci_get_device(i), &root_volume)) {
            root_mounted = 1;
            break;
        }
    }
#endif

    if (!root_mounted) {
#ifdef CONFIG_STORAGE_IDE
        for (uint32_t i = 0; i < ide_device_count(); i++) {
            if (mount_first_fs(ide_get_device(i), &root_volume)) {
                root_mounted = 1;
                break;
            }
        }
#endif
    }

    if (!root_mounted) {
        kpanic("[Peanut kernel - panic - no mountable filesystem volume!]");
    }

    if (!validate_fhs(&root_volume)) {
        kpanic("[Peanut kernel - panic - FHS not working! Could not find /bin, /usr, /lib or /boot.]");
    }

    kprint("FHS found... yes\n");

#ifdef CONFIG_STORAGE_TESTS
    storage_tests_run(&root_volume);
#endif
}
