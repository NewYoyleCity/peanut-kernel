#include "drivers/block/ide.h"
#include "drivers/bus/io.h"

#define ATA_REG_DATA       0x00
#define ATA_REG_ERROR      0x01
#define ATA_REG_SECCOUNT0  0x02
#define ATA_REG_LBA0       0x03
#define ATA_REG_LBA1       0x04
#define ATA_REG_LBA2       0x05
#define ATA_REG_HDDEVSEL   0x06
#define ATA_REG_COMMAND    0x07
#define ATA_REG_STATUS     0x07

#define ATA_SR_BSY 0x80
#define ATA_SR_DRDY 0x40
#define ATA_SR_DRQ 0x08
#define ATA_SR_ERR 0x01

#define ATA_CMD_IDENTIFY     0xEC
#define ATA_CMD_READ_SECTORS  0x20
#define ATA_CMD_WRITE_SECTORS 0x30
#define ATA_CMD_CACHE_FLUSH   0xE7

typedef struct {
    uint16_t io_base;
    uint16_t ctrl_base;
    uint8_t slave;
} IdeDeviceData;

static IdeDeviceData ide_data[IDE_MAX_DEVICES];
static BlockDevice ide_devices[IDE_MAX_DEVICES];
static uint32_t ide_devices_found = 0;

static void ide_delay(IdeDeviceData* ide) {
    inb(ide->ctrl_base);
    inb(ide->ctrl_base);
    inb(ide->ctrl_base);
    inb(ide->ctrl_base);
}

static int ide_wait(IdeDeviceData* ide, uint8_t want_drq) {
    for (uint32_t i = 0; i < 100000; i++) {
        uint8_t status = inb(ide->io_base + ATA_REG_STATUS);
        if (status & ATA_SR_ERR) return -1;
        if (!(status & ATA_SR_BSY)) {
            if (!want_drq || (status & ATA_SR_DRQ)) return 0;
        }
    }

    return -1;
}

static int ide_identify(IdeDeviceData* ide, uint16_t* identify) {
    outb(ide->io_base + ATA_REG_HDDEVSEL, (uint8_t)(0xA0 | (ide->slave << 4)));
    ide_delay(ide);
    outb(ide->io_base + ATA_REG_SECCOUNT0, 0);
    outb(ide->io_base + ATA_REG_LBA0, 0);
    outb(ide->io_base + ATA_REG_LBA1, 0);
    outb(ide->io_base + ATA_REG_LBA2, 0);
    outb(ide->io_base + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);
    ide_delay(ide);

    uint8_t status = inb(ide->io_base + ATA_REG_STATUS);
    if (status == 0) return -1;

    if (ide_wait(ide, 1) != 0) return -1;
    insw(ide->io_base + ATA_REG_DATA, identify, 256);
    return 0;
}

static int ide_read(BlockDevice* dev, uint64_t lba, uint32_t count, void* buffer) {
    IdeDeviceData* ide = (IdeDeviceData*)dev->driver_data;
    uint8_t* out = (uint8_t*)buffer;

    if (lba > 0x0FFFFFFF || count > 255) return -1;

    for (uint32_t sector = 0; sector < count; sector++) {
        uint32_t current_lba = (uint32_t)(lba + sector);

        if (ide_wait(ide, 0) != 0) return -1;
        outb(ide->io_base + ATA_REG_HDDEVSEL,
             (uint8_t)(0xE0 | (ide->slave << 4) | ((current_lba >> 24) & 0x0F)));
        outb(ide->io_base + ATA_REG_SECCOUNT0, 1);
        outb(ide->io_base + ATA_REG_LBA0, (uint8_t)current_lba);
        outb(ide->io_base + ATA_REG_LBA1, (uint8_t)(current_lba >> 8));
        outb(ide->io_base + ATA_REG_LBA2, (uint8_t)(current_lba >> 16));
        outb(ide->io_base + ATA_REG_COMMAND, ATA_CMD_READ_SECTORS);

        if (ide_wait(ide, 1) != 0) return -1;
        insw(ide->io_base + ATA_REG_DATA, out + sector * BLOCK_SECTOR_SIZE, 256);
    }

    return 0;
}

static int ide_write(BlockDevice* dev, uint64_t lba, uint32_t count, const void* buffer) {
    IdeDeviceData* ide = (IdeDeviceData*)dev->driver_data;
    const uint8_t* in = (const uint8_t*)buffer;

    if (lba > 0x0FFFFFFF || count > 255) return -1;

    for (uint32_t sector = 0; sector < count; sector++) {
        uint32_t current_lba = (uint32_t)(lba + sector);

        if (ide_wait(ide, 0) != 0) return -1;
        outb(ide->io_base + ATA_REG_HDDEVSEL,
             (uint8_t)(0xE0 | (ide->slave << 4) | ((current_lba >> 24) & 0x0F)));
        outb(ide->io_base + ATA_REG_SECCOUNT0, 1);
        outb(ide->io_base + ATA_REG_LBA0, (uint8_t)current_lba);
        outb(ide->io_base + ATA_REG_LBA1, (uint8_t)(current_lba >> 8));
        outb(ide->io_base + ATA_REG_LBA2, (uint8_t)(current_lba >> 16));
        outb(ide->io_base + ATA_REG_COMMAND, ATA_CMD_WRITE_SECTORS);

        if (ide_wait(ide, 1) != 0) return -1;
        outsw(ide->io_base + ATA_REG_DATA, in + sector * BLOCK_SECTOR_SIZE, 256);
    }

    outb(ide->io_base + ATA_REG_COMMAND, ATA_CMD_CACHE_FLUSH);
    return ide_wait(ide, 0);
}

int ide_init() {
    uint16_t identify[256];
    const uint16_t io_bases[2] = { 0x1F0, 0x170 };
    const uint16_t ctrl_bases[2] = { 0x3F6, 0x376 };

    ide_devices_found = 0;
    for (uint8_t channel = 0; channel < 2; channel++) {
        for (uint8_t slave = 0; slave < 2; slave++) {
            if (ide_devices_found >= IDE_MAX_DEVICES) return (int)ide_devices_found;

            IdeDeviceData probe = { io_bases[channel], ctrl_bases[channel], slave };
            if (ide_identify(&probe, identify) != 0) continue;

            IdeDeviceData* data = &ide_data[ide_devices_found];
            *data = probe;

            BlockDevice* dev = &ide_devices[ide_devices_found];
            dev->name = "ide";
            dev->sector_size = BLOCK_SECTOR_SIZE;
            dev->sector_count = ((uint32_t)identify[61] << 16) | identify[60];
            dev->driver_data = data;
            dev->read = ide_read;
            dev->write = ide_write;
            ide_devices_found++;
        }
    }

    return (int)ide_devices_found;
}

uint32_t ide_device_count() {
    return ide_devices_found;
}

BlockDevice* ide_get_device(uint32_t index) {
    if (index >= ide_devices_found) return NULL;
    return &ide_devices[index];
}
