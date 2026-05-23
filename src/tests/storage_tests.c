/* storage_tests.c -- Post-boot storage subsystem self-tests.
 *
 * Validates block reads, MBR scanning, FHS directory existence, and
 * optionally performs write/read-back tests on FAT32/exFAT/ext volumes.
 */

#include "tests/storage_tests.h"
#include "freelib/kpanic.h"
#include "freelib/kstdio.h"
#include "fs/partition.h"
#include "fs/vfs.h"
#include "config.h"
#include "drivers/block/ide.h"
#include "drivers/block/ahci.h"


/* memeq -- compare two memory regions for equality.
 */static int memeq(const uint8_t* a, const uint8_t* b, uint32_t len) {
    for (uint32_t i = 0; i < len; i++) {
        if (a[i] != b[i])
            return 0;
    }
    return 1;
}


/* expect -- assert condition; panic on failure with message.
 */static void expect(int condition, const char* message) {
    if (!condition)
        kpanic(message);
}


/* expect_root_dir_fat32 -- assert a FAT32 root directory exists.
 */static void expect_root_dir_fat32(Fat32Volume* volume, const char* name) {
    Fat32DirEntry entry;
    expect(fat32_find_root(volume, name, &entry) == 0, "storage test: missing FHS directory");
    expect((entry.attributes & FAT32_ATTR_DIRECTORY) != 0, "storage test: FHS entry is not a directory");
}


/* expect_root_dir_exfat -- assert an exFAT root directory exists.
 */static void expect_root_dir_exfat(ExfatVolume* volume, const char* name) {
    ExfatDirEntry entry;
    expect(exfat_find_root(volume, name, &entry) == 0, "storage test: missing FHS directory");
    expect((entry.attributes & EXFAT_ATTR_DIRECTORY) != 0, "storage test: FHS entry is not a directory");
}


/* expect_root_dir_ext -- assert a ext root directory exists.
 */static void expect_root_dir_ext(ExtVolume* volume, const char* name) {
    char p[16];
    uint32_t i = 0;
    p[i++] = '/';
    for (uint32_t j = 0; name[j] && i + 1 < sizeof(p); j++)
        p[i++] = name[j];
    p[i] = '\0';
    expect(extfs_dir_exists(volume, p), "storage test: missing FHS directory");
}

void storage_tests_run(PeanutVolume* volume) {
    Partition partitions[PARTITION_MAX];
    uint8_t sector[BLOCK_SECTOR_SIZE];
    uint8_t read_back[64];
    const uint8_t write_payload[] = "PEANUT-STORAGE-TEST\n";
    uint32_t bytes_read = 0;

    kprint("    Storage tests:\n");

    expect(volume != NULL, "storage test: volume is NULL");

    BlockDevice* disk;
    const Partition* part;
    if (volume->fs_kind == PEANUT_FS_EXFAT) {
        disk = volume->exfat.partition.disk;
        part = &volume->exfat.partition;
    } else if (volume->fs_kind == PEANUT_FS_EXT) {
        disk = volume->ext.partition.disk;
        part = &volume->ext.partition;
    } else {
        disk = volume->fat32.partition.disk;
        part = &volume->fat32.partition;
    }

    expect(block_read(disk, part->first_lba, 1, sector) == 0, "storage test: block read failed");
    expect(sector[510] == 0x55 && sector[511] == 0xAA, "storage test: boot sector signature missing");
    kprint("      block read ok\n");

    expect(partition_scan_mbr(disk, partitions, PARTITION_MAX) > 0, "storage test: MBR scan failed");

    if (volume->fs_kind == PEANUT_FS_FAT32) {
        expect(partition_is_fat32(part), "storage test: mounted partition is not FAT32");
        kprint("      MBR/FAT32 partition ok\n");

        expect_root_dir_fat32(&volume->fat32, "BOOT");
        expect_root_dir_fat32(&volume->fat32, "BIN");
        expect_root_dir_fat32(&volume->fat32, "USR");
        expect_root_dir_fat32(&volume->fat32, "LIB");
        kprint("      FHS dirs ok\n");

#ifdef CONFIG_ADV_WRITE_TEST
        Fat32DirEntry probe;
        if (fat32_find_root(&volume->fat32, "TEST.TXT", &probe) != 0) {
            kprint("      write test skipped: TEST.TXT missing\n");
            return;
        }

        expect(fat32_write_file(&volume->fat32, "TEST.TXT", write_payload, sizeof(write_payload) - 1) == 0,
            "storage test: FAT32 write failed");
        expect(fat32_read_file(&volume->fat32, "TEST.TXT", read_back, sizeof(read_back), &bytes_read) == 0,
            "storage test: FAT32 readback failed");
        expect(bytes_read >= sizeof(write_payload) - 1, "storage test: FAT32 readback too short");
        expect(memeq(read_back, write_payload, sizeof(write_payload) - 1),
            "storage test: FAT32 write/read mismatch");
        kprint("      FAT32 write/read ok\n");
        kprint("      FAT32 write test enabled\n");
#else
        (void)read_back;
        (void)write_payload;
        (void)bytes_read;
#endif
    } else if (volume->fs_kind == PEANUT_FS_EXFAT) {
        expect(exfat_probe_boot(disk, part->first_lba), "storage test: mounted volume is not exFAT");
        kprint("      MBR/exFAT partition ok\n");

        expect_root_dir_exfat(&volume->exfat, "BOOT");
        expect_root_dir_exfat(&volume->exfat, "BIN");
        expect_root_dir_exfat(&volume->exfat, "USR");
        expect_root_dir_exfat(&volume->exfat, "LIB");
        kprint("      FHS dirs ok\n");
#if defined(CONFIG_ADV_WRITE_TEST) && defined(CONFIG_FS_EXFAT_WRITE)
        ExfatDirEntry probe;
        if (exfat_find_root(&volume->exfat, "TEST.TXT", &probe) != 0) {
            kprint("      exFAT write test skipped: TEST.TXT missing\n");
            return;
        }
        expect(exfat_write_file(&volume->exfat, "TEST.TXT", write_payload, sizeof(write_payload) - 1) == 0,
            "storage test: exFAT write failed");
        expect(exfat_read_file(&volume->exfat, "TEST.TXT", read_back, sizeof(read_back), &bytes_read) == 0,
            "storage test: exFAT readback failed");
        expect(bytes_read >= sizeof(write_payload) - 1, "storage test: exFAT readback too short");
        expect(memeq(read_back, write_payload, sizeof(write_payload) - 1),
            "storage test: exFAT write/read mismatch");
        kprint("      exFAT write/read ok\n");
#endif
    } else {
        expect(extfs_probe(disk, part->first_lba), "storage test: mounted volume is not ext");
        kprint("      MBR/ext partition ok\n");

        expect_root_dir_ext(&volume->ext, "BOOT");
        expect_root_dir_ext(&volume->ext, "BIN");
        expect_root_dir_ext(&volume->ext, "USR");
        expect_root_dir_ext(&volume->ext, "LIB");
        kprint("      FHS dirs ok\n");
#if defined(CONFIG_ADV_WRITE_TEST) && (defined(CONFIG_FS_EXT4_WRITE) || defined(CONFIG_FS_EXT3_WRITE) || defined(CONFIG_FS_EXT2_WRITE))
        uint64_t sz = 0;
        int is_dir = 0;
        if (extfs_stat(&volume->ext, "/TEST.TXT", &sz, &is_dir) != 0 || is_dir) {
            kprint("      ext write test skipped: /TEST.TXT missing\n");
            return;
        }
        expect(extfs_write_file(&volume->ext, "/TEST.TXT", write_payload, sizeof(write_payload) - 1) == 0,
            "storage test: ext write failed");
        expect(extfs_read_file(&volume->ext, "/TEST.TXT", read_back, sizeof(read_back), &bytes_read) == 0,
            "storage test: ext readback failed");
        expect(bytes_read >= sizeof(write_payload) - 1, "storage test: ext readback too short");
        expect(memeq(read_back, write_payload, sizeof(write_payload) - 1),
            "storage test: ext write/read mismatch");
        kprint("      ext write/read ok\n");
#endif
    }

#if defined(CONFIG_STORAGE_IDE) && defined(CONFIG_STORAGE_AHCI)
    if (ide_device_count() > 0 && ahci_device_count() > 0) {
        uint8_t a[BLOCK_SECTOR_SIZE];
        uint8_t b[BLOCK_SECTOR_SIZE];
        BlockDevice* ide = ide_get_device(0);
        BlockDevice* sat = ahci_get_device(0);
        expect(block_read(ide, 0, 1, a) == 0, "storage test: IDE read with AHCI present failed");
        expect(block_read(sat, 0, 1, b) == 0, "storage test: AHCI read with IDE present failed");
        expect(a[510] == 0x55 && a[511] == 0xAA, "storage test: IDE MBR sig");
        expect(b[510] == 0x55 && b[511] == 0xAA, "storage test: AHCI MBR sig");
        kprint("      dual IDE+AHCI read ok\n");
    }
#endif

    uint8_t zbuf[8];
    uint32_t zr = 0;
    expect(vfs_pseudo_read("/dev/zero", zbuf, sizeof(zbuf), &zr) == 0, "storage test: /dev/zero");
    expect(zr == sizeof(zbuf), "storage test: /dev/zero len");
    expect(zbuf[0] == 0 && zbuf[7] == 0, "storage test: /dev/zero bytes");
    kprint("      vfs /dev/zero ok\n");
}
