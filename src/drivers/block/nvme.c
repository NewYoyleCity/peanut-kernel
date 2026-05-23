/* nvme.c -- NVMe solid-state disk driver.
 *
 * Initialises an NVMe controller via PCI BAR0, creates admin and I/O
 * submission/completion queues, identifies the first namespace, and
 * exposes it as a BlockDevice.
 */

#include "drivers/block/nvme.h"
#include "drivers/bus/pci.h"
#include "drivers/bus/io.h"
#include "freelib/kstdio.h"
#include "freelib/kalloc.h"
#include "freelib/kstdint.h"
#include "mm/vm.h"

#define NVME_PCI_CLASS 0x01
#define NVME_PCI_SUBCLASS 0x08
#define NVME_PCI_PROGIF 0x02

#define NVME_CAP_MQES 0xFFFF
#define NVME_CAP_TO_m1 24

#define NVME_CC_EN 1u
#define NVME_CC_IOSQES 6
#define NVME_CC_IOCQES 4

#define NVME_CSTS_RDY (1u << 0)

#define NVME_DBL_SQ0T 0x1000
#define NVME_DBL_CQ0H 0x1000

#define NVME_ADMIN_QUEUE_SIZE 64
#define NVME_IO_QUEUE_SIZE 64
#define NVME_MAX_NAMESPACES 8

#define NVME_CMD_IDENTIFY 6
#define NVME_CMD_CREATE_SQ 1
#define NVME_CMD_CREATE_CQ 5
#define NVME_CMD_READ 2
#define NVME_CMD_WRITE 1

static BlockDevice nvme_devices[NVME_MAX_NAMESPACES];
static uint32_t nvme_count;

typedef volatile struct {
    uint64_t cap;
    uint32_t vs;
    uint32_t intms;
    uint32_t intmc;
    uint32_t cc;
    uint32_t rsvd1;
    uint32_t csts;
    uint32_t rsvd2;
    uint32_t aqa;
    uint64_t asq;
    uint64_t acq;
} NvmeRegs;

typedef struct {
    uint32_t cdw0;
    uint32_t nsid;
    uint64_t prp1;
    uint64_t prp2;
    uint32_t cdw10;
    uint32_t cdw11;
    uint32_t cdw12;
    uint32_t cdw13;
    uint32_t cdw14;
    uint32_t cdw15;
} __attribute__((packed)) NvmeCmd;

typedef struct {
    uint32_t dw0;
    uint32_t dw1;
    uint32_t dw2;
    uint32_t dw3;
    uint16_t sqhd;
    uint16_t sqid;
    uint16_t cid;
    uint16_t p;
} __attribute__((packed)) NvmeCqe;

typedef struct {
    uint64_t nsze;
    uint64_t ncap;
    uint64_t nuse;
    uint8_t  nsfeat;
    uint8_t  nlbaf;
    uint8_t  flbas;
    uint8_t  mc;
    uint8_t  dpc;
    uint8_t  dps;
    uint8_t  nmic;
    uint8_t  rescap;
    uint8_t  fpi;
    uint8_t  dlfeat;
    uint8_t  nawun;
    uint8_t  nawupf;
    uint8_t  nacwu;
    uint8_t  nabsn;
    uint8_t  nabo;
    uint8_t  nabspf;
    uint16_t noiob;
    uint64_t nvmcap[2];
    uint8_t  rsvd[40];
    uint8_t  nguid[16];
    uint8_t  eui64[8];
    uint32_t lba_format[32];
} __attribute__((packed)) NvmeIdNs;

typedef struct {
    uint64_t prp1;
    uint64_t prp2;
    uint32_t nlb;
    uint32_t rsvd;
} __attribute__((packed)) NvmeRwCmd;

typedef struct {
    NvmeRegs* regs;
    uint8_t* mem;
    NvmeCmd* sq;
    NvmeCqe* cq;
    uint32_t sq_tail;
    uint32_t cq_head;
    uint32_t ns_count;
    uint64_t ns_lba[NVME_MAX_NAMESPACES];
    uint32_t ns_lba_count[NVME_MAX_NAMESPACES];
} NvmeData;

static NvmeData nvme_data;


/* nvme_doorbell -- ring a submission/completion queue doorbell.
 */static void nvme_doorbell(NvmeData* d, uint32_t qid, uint32_t val, int cq) {
    volatile uint32_t* db = (volatile uint32_t*)((uint8_t*)d->regs + 0x1000 + (qid * 8u) + (cq ? 4u : 0u));
    *db = val;
}


/* nvme_submit_cmd -- submit admin command and wait for completion.
 */static int nvme_submit_cmd(NvmeData* d, NvmeCmd* cmd) {
    uint32_t i = d->sq_tail % NVME_ADMIN_QUEUE_SIZE;
    d->sq[i] = *cmd;
    d->sq_tail++;
    nvme_doorbell(d, 0, d->sq_tail, 0);
    uint32_t spin = 0;
    while (spin < 10000000) {
        if (d->cq[d->cq_head % NVME_ADMIN_QUEUE_SIZE].p & 1u) {
            uint32_t status = d->cq[d->cq_head % NVME_ADMIN_QUEUE_SIZE].dw3;
            d->cq[d->cq_head % NVME_ADMIN_QUEUE_SIZE].p = 0;
            d->cq_head++;
            nvme_doorbell(d, 0, d->cq_head, 1);
            return (status >> 1) & 0xFFu;
        }
        spin++;
    }
    return 0xFF;
}

static int nvme_ns_read(BlockDevice* dev, uint64_t lba, uint32_t count, void* buffer) {
    (void)dev;
    NvmeCmd cmd;
    cmd.cdw0 = NVME_CMD_READ;
    cmd.nsid = 1;
    cmd.prp1 = (uint64_t)(uintptr_t)buffer;
    cmd.prp2 = 0;
    cmd.cdw10 = (uint32_t)lba;
    cmd.cdw11 = (uint32_t)(lba >> 32);
    cmd.cdw12 = (count - 1) | (0 << 16);
    cmd.cdw13 = 0;
    cmd.cdw14 = 0;
    cmd.cdw15 = 0;
    return nvme_submit_cmd(&nvme_data, &cmd);
}

static int nvme_ns_write(BlockDevice* dev, uint64_t lba, uint32_t count, const void* buffer) {
    (void)dev;
    NvmeCmd cmd;
    cmd.cdw0 = NVME_CMD_WRITE;
    cmd.nsid = 1;
    cmd.prp1 = (uint64_t)(uintptr_t)buffer;
    cmd.prp2 = 0;
    cmd.cdw10 = (uint32_t)lba;
    cmd.cdw11 = (uint32_t)(lba >> 32);
    cmd.cdw12 = (count - 1);
    cmd.cdw13 = 0;
    cmd.cdw14 = 0;
    cmd.cdw15 = 0;
    return nvme_submit_cmd(&nvme_data, &cmd);
}

int nvme_init(void) {
    PciAddress addr;
    if (pci_find_class(NVME_PCI_CLASS, NVME_PCI_SUBCLASS, NVME_PCI_PROGIF, &addr) != 0) {
        kprint("NVMe: no PCI device found\n");
        return 0;
    }

    uint32_t cmdsts = pci_read32(addr, 0x04);
    pci_write32(addr, 0x04, cmdsts | (1u << 1) | (1u << 2));

    uint64_t bar = pci_bar(addr, 0);
    if (!bar) {
        kprint("NVMe: invalid BAR\n");
        return 0;
    }

    NvmeRegs* regs = (NvmeRegs*)(uintptr_t)bar;
    nvme_data.regs = regs;

    uint32_t cc_val = 0;
    regs->cc = cc_val;

    for (uint32_t spin = 0; spin < 1000000; spin++) {
        if (regs->csts & NVME_CSTS_RDY) break;
    }

    uint8_t* mem = (uint8_t*)kalloc(4096 * 4);
    if (!mem) {
        kprint("NVMe: alloc failed\n");
        return 0;
    }
    for (uint32_t i = 0; i < 4096 * 4; i++) mem[i] = 0;
    nvme_data.mem = mem;

    nvme_data.sq = (NvmeCmd*)mem;
    nvme_data.cq = (NvmeCqe*)(mem + 4096);

    regs->aqa = (NVME_ADMIN_QUEUE_SIZE - 1) | ((NVME_ADMIN_QUEUE_SIZE - 1) << 16);
    regs->asq = (uint64_t)(uintptr_t)nvme_data.sq;
    regs->acq = (uint64_t)(uintptr_t)nvme_data.cq;
    nvme_data.sq_tail = 0;
    nvme_data.cq_head = 0;

    regs->cc = NVME_CC_EN | (NVME_CC_IOSQES << 20) | (NVME_CC_IOCQES << 16);
    for (uint32_t spin = 0; spin < 10000000; spin++) {
        if (regs->csts & NVME_CSTS_RDY) break;
    }

    NvmeCmd id_cmd;
    id_cmd.cdw0 = NVME_CMD_IDENTIFY;
    id_cmd.nsid = 0;
    id_cmd.prp1 = (uint64_t)(uintptr_t)(mem + 8192);
    id_cmd.prp2 = 0;
    id_cmd.cdw10 = 1;
    id_cmd.cdw11 = 0;
    id_cmd.cdw12 = 0;
    id_cmd.cdw13 = 0;
    id_cmd.cdw14 = 0;
    id_cmd.cdw15 = 0;

    if (nvme_submit_cmd(&nvme_data, &id_cmd) != 0) {
        kprint("NVMe: identify failed\n");
        return 0;
    }

    NvmeIdNs* ns = (NvmeIdNs*)(mem + 8192);
    if (ns->nsze == 0) {
        kprint("NVMe: no active namespace\n");
        return 0;
    }

    int lba_idx = ns->flbas & 0xFu;
    uint32_t lba_size = 1u << ((ns->lba_format[lba_idx] >> 16) & 0xFFu);

    nvme_count = 1;
    BlockDevice* dev = &nvme_devices[0];
    dev->name = "nvme";
    dev->sector_count = (uint64_t)ns->nsze;
    dev->sector_size = lba_size;
    dev->driver_data = &nvme_data;
    dev->read = nvme_ns_read;
    dev->write = nvme_ns_write;

    kprint("NVMe: drive found, ");
    kprint_int((uint32_t)(ns->nsze));
    kprint(" sectors of ");
    kprint_int(lba_size);
    kprint(" bytes\n");
    return 1;
}

uint32_t nvme_device_count(void) {
    return nvme_count;
}

BlockDevice* nvme_get_device(uint32_t index) {
    if (index >= nvme_count) return NULL;
    return &nvme_devices[index];
}
