#include "drivers/block/ahci.h"
#include "drivers/bus/pci.h"

#define AHCI_CLASS_MASS_STORAGE 0x01
#define AHCI_SUBCLASS_SATA      0x06
#define AHCI_PROGIF_AHCI        0x01

#define HBA_PORT_DET_PRESENT 3
#define HBA_PORT_IPM_ACTIVE  1
#define SATA_SIG_ATA         0x00000101

#define HBA_PxCMD_ST  0x0001
#define HBA_PxCMD_FRE 0x0010
#define HBA_PxCMD_FR  0x4000
#define HBA_PxCMD_CR  0x8000

#define FIS_TYPE_REG_H2D 0x27
#define ATA_CMD_READ_DMA_EXT 0x25
#define ATA_CMD_WRITE_DMA_EXT 0x35

typedef volatile struct {
    uint32_t clb;
    uint32_t clbu;
    uint32_t fb;
    uint32_t fbu;
    uint32_t is;
    uint32_t ie;
    uint32_t cmd;
    uint32_t reserved0;
    uint32_t tfd;
    uint32_t sig;
    uint32_t ssts;
    uint32_t sctl;
    uint32_t serr;
    uint32_t sact;
    uint32_t ci;
    uint32_t sntf;
    uint32_t fbs;
    uint32_t reserved1[11];
    uint32_t vendor[4];
} HbaPort;

typedef volatile struct {
    uint32_t cap;
    uint32_t ghc;
    uint32_t is;
    uint32_t pi;
    uint32_t vs;
    uint32_t ccc_ctl;
    uint32_t ccc_pts;
    uint32_t em_loc;
    uint32_t em_ctl;
    uint32_t cap2;
    uint32_t bohc;
    uint8_t reserved[0xA0 - 0x2C];
    uint8_t vendor[0x100 - 0xA0];
    HbaPort ports[32];
} HbaMemory;

typedef struct {
    uint8_t fis_type;
    uint8_t pmport:4;
    uint8_t reserved0:3;
    uint8_t c:1;
    uint8_t command;
    uint8_t featurel;
    uint8_t lba0;
    uint8_t lba1;
    uint8_t lba2;
    uint8_t device;
    uint8_t lba3;
    uint8_t lba4;
    uint8_t lba5;
    uint8_t featureh;
    uint8_t countl;
    uint8_t counth;
    uint8_t icc;
    uint8_t control;
    uint8_t reserved1[4];
} FisRegH2D;

typedef struct {
    uint32_t dba;
    uint32_t dbau;
    uint32_t reserved0;
    uint32_t dbc;
} HbaPrdtEntry;

typedef struct {
    uint8_t cfis[64];
    uint8_t acmd[16];
    uint8_t reserved[48];
    HbaPrdtEntry prdt_entry[1];
} HbaCommandTable;

typedef struct {
    uint8_t cfl:5;
    uint8_t a:1;
    uint8_t w:1;
    uint8_t p:1;
    uint8_t r:1;
    uint8_t b:1;
    uint8_t c:1;
    uint8_t reserved0:1;
    uint8_t pmp:4;
    uint16_t prdtl;
    uint32_t prdbc;
    uint32_t ctba;
    uint32_t ctbau;
    uint32_t reserved1[4];
} HbaCommandHeader;

typedef struct {
    HbaMemory* abar;
    HbaPort* port;
    uint8_t port_index;
    HbaCommandHeader command_list[32] __attribute__((aligned(1024)));
    uint8_t fis_area[256] __attribute__((aligned(256)));
    HbaCommandTable command_table __attribute__((aligned(256)));
} AhciPortData;

static AhciPortData ahci_ports[AHCI_MAX_DEVICES];
static BlockDevice ahci_devices[AHCI_MAX_DEVICES];
static uint32_t ahci_count = 0;

static void memzero(void* ptr, uint32_t len) {
    uint8_t* bytes = (uint8_t*)ptr;
    for (uint32_t i = 0; i < len; i++) bytes[i] = 0;
}

static void stop_port(HbaPort* port) {
    port->cmd &= ~HBA_PxCMD_ST;
    port->cmd &= ~HBA_PxCMD_FRE;

    for (uint32_t i = 0; i < 100000; i++) {
        if ((port->cmd & (HBA_PxCMD_FR | HBA_PxCMD_CR)) == 0) break;
    }
}

static void start_port(HbaPort* port) {
    while (port->cmd & HBA_PxCMD_CR) {}
    port->cmd |= HBA_PxCMD_FRE;
    port->cmd |= HBA_PxCMD_ST;
}

static int port_is_sata(HbaPort* port) {
    uint32_t ssts = port->ssts;
    uint8_t det = ssts & 0x0F;
    uint8_t ipm = (ssts >> 8) & 0x0F;
    return det == HBA_PORT_DET_PRESENT &&
        ipm == HBA_PORT_IPM_ACTIVE &&
        port->sig == SATA_SIG_ATA;
}

static int ahci_transfer(BlockDevice* dev, uint64_t lba, uint32_t count, void* buffer, uint8_t write) {
    AhciPortData* pd = (AhciPortData*)dev->driver_data;
    HbaPort* port = pd->port;

    if (count == 0 || count > 128) return -1;

    port->is = 0xFFFFFFFF;
    memzero(&pd->command_table, sizeof(pd->command_table));

    HbaCommandHeader* header = &pd->command_list[0];
    header->cfl = sizeof(FisRegH2D) / sizeof(uint32_t);
    header->w = write ? 1 : 0;
    header->prdtl = 1;
    header->ctba = (uint32_t)(uintptr_t)&pd->command_table;
    header->ctbau = 0;

    pd->command_table.prdt_entry[0].dba = (uint32_t)(uintptr_t)buffer;
    pd->command_table.prdt_entry[0].dbau = 0;
    pd->command_table.prdt_entry[0].dbc = (count * BLOCK_SECTOR_SIZE) - 1;

    FisRegH2D* fis = (FisRegH2D*)pd->command_table.cfis;
    fis->fis_type = FIS_TYPE_REG_H2D;
    fis->c = 1;
    fis->command = write ? ATA_CMD_WRITE_DMA_EXT : ATA_CMD_READ_DMA_EXT;
    fis->device = 1 << 6;
    fis->lba0 = (uint8_t)lba;
    fis->lba1 = (uint8_t)(lba >> 8);
    fis->lba2 = (uint8_t)(lba >> 16);
    fis->lba3 = (uint8_t)(lba >> 24);
    fis->lba4 = (uint8_t)(lba >> 32);
    fis->lba5 = (uint8_t)(lba >> 40);
    fis->countl = (uint8_t)count;
    fis->counth = (uint8_t)(count >> 8);

    for (uint32_t spin = 0; spin < 100000; spin++) {
        if ((port->tfd & (0x80 | 0x08)) == 0) break;
        if (spin == 99999) return -1;
    }

    port->ci = 1;

    for (;;) {
        if ((port->ci & 1) == 0) break;
        if (port->is & (1 << 30)) return -1;
    }

    if (port->is & (1 << 30)) return -1;
    return 0;
}

static int ahci_read(BlockDevice* dev, uint64_t lba, uint32_t count, void* buffer) {
    return ahci_transfer(dev, lba, count, buffer, 0);
}

static int ahci_write(BlockDevice* dev, uint64_t lba, uint32_t count, const void* buffer) {
    return ahci_transfer(dev, lba, count, (void*)buffer, 1);
}

int ahci_init() {
    PciAddress addr;
    ahci_count = 0;

    if (pci_find_class(AHCI_CLASS_MASS_STORAGE, AHCI_SUBCLASS_SATA, AHCI_PROGIF_AHCI, &addr) != 0) {
        return 0;
    }

    uint32_t command = pci_read32(addr, 0x04);
    command |= (1 << 1) | (1 << 2);
    pci_write32(addr, 0x04, command);

    uint32_t bar5 = pci_read32(addr, 0x24) & 0xFFFFFFF0;
    if (bar5 == 0) return 0;

    HbaMemory* abar = (HbaMemory*)(uintptr_t)bar5;
    abar->ghc |= (1 << 31);

    uint32_t implemented = abar->pi;
    for (uint8_t i = 0; i < 32 && ahci_count < AHCI_MAX_DEVICES; i++) {
        if ((implemented & (1u << i)) == 0) continue;

        HbaPort* port = &abar->ports[i];
        if (!port_is_sata(port)) continue;

        stop_port(port);
        AhciPortData* pd = &ahci_ports[ahci_count];
        memzero(pd, sizeof(*pd));
        pd->abar = abar;
        pd->port = port;
        pd->port_index = i;

        memzero(pd->command_list, sizeof(pd->command_list));
        memzero(pd->fis_area, sizeof(pd->fis_area));

        port->clb = (uint32_t)(uintptr_t)pd->command_list;
        port->clbu = 0;
        port->fb = (uint32_t)(uintptr_t)pd->fis_area;
        port->fbu = 0;

        pd->command_list[0].ctba = (uint32_t)(uintptr_t)&pd->command_table;
        pd->command_list[0].ctbau = 0;

        port->serr = 0xFFFFFFFF;
        port->is = 0xFFFFFFFF;
        start_port(port);

        BlockDevice* dev = &ahci_devices[ahci_count];
        dev->name = "ahci";
        dev->sector_size = BLOCK_SECTOR_SIZE;
        dev->sector_count = 0;
        dev->driver_data = pd;
        dev->read = ahci_read;
        dev->write = ahci_write;
        ahci_count++;
    }

    return (int)ahci_count;
}

uint32_t ahci_device_count() {
    return ahci_count;
}

BlockDevice* ahci_get_device(uint32_t index) {
    if (index >= ahci_count) return NULL;
    return &ahci_devices[index];
}
