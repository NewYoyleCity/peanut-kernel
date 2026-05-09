#include "e1000.h"
#include "net.h"
#include "freelib/kstdio.h"
#include "freelib/kalloc.h"
#include "drivers/bus/pci.h"

#define E1000_REG_CTRL   0x0000
#define E1000_REG_STATUS 0x0008
#define E1000_REG_EERD   0x0014
#define E1000_REG_CTRL_EXT 0x0018
#define E1000_REG_IMS    0x00D0
#define E1000_REG_ICR    0x00C0
#define E1000_REG_RCTL   0x0100
#define E1000_REG_TCTL   0x0400
#define E1000_REG_TDBAL  0x3800
#define E1000_REG_TDBAH  0x3804
#define E1000_REG_TDLEN  0x3808
#define E1000_REG_TDH    0x3810
#define E1000_REG_TDT    0x3818
#define E1000_REG_RAL    0x5400
#define E1000_REG_RAH    0x5404
#define E1000_REG_RDBAL  0x2800
#define E1000_REG_RDBAH  0x2804
#define E1000_REG_RDLEN  0x2808
#define E1000_REG_RDH    0x2810
#define E1000_REG_RDT    0x2818
#define E1000_REG_RDTR   0x2820
#define E1000_REG_RDBAL  0x2800

#define E1000_CTRL_LRST 0x00000040
#define E1000_CTRL_RST  0x00004000
#define E1000_CTRL_ASDE 0x00000020
#define E1000_CTRL_SLU  0x00000040
#define E1000_CTRL_FD   0x00000001

#define E1000_STATUS_LU 0x00000002

#define E1000_RCTL_EN   0x00000002
#define E1000_RCTL_BAM  0x00008000
#define E1000_RCTL_SZ_2048 0x00000000
#define E1000_RCTL_SECRC 0x04000000

#define E1000_TCTL_EN   0x00000002
#define E1000_TCTL_PSP  0x00000008
#define E1000_TCTL_CT   0x0000000F
#define E1000_TCTL_COLD 0x0003F000
#define E1000_TCTL_COLD_SHIFT 12

#define E1000_RX_DESC_COUNT 128
#define E1000_TX_DESC_COUNT 128

typedef struct {
    uint64_t addr;
    uint16_t length;
    uint16_t cso;
    uint8_t  cmd;
    uint8_t  status;
    uint16_t css;
    uint16_t special;
} __attribute__((packed)) e1000_tx_desc_t;

typedef struct {
    uint64_t addr;
    uint16_t length;
    uint16_t cso;
    uint8_t  cmd;
    uint8_t  status;
    uint16_t css;
    uint16_t special;
} __attribute__((packed)) e1000_rx_desc_t;

static volatile uint32_t *e1000_mmio = NULL;
static e1000_tx_desc_t *tx_descs = NULL;
static e1000_rx_desc_t *rx_descs = NULL;
static uint8_t *rx_buffers = NULL;
static uint8_t *tx_buffers = NULL;
static net_device_t e1000_dev;

static uint32_t e1000_read_reg(uint16_t reg) {
    if (e1000_mmio) {
        return e1000_mmio[reg / 4];
    }
    return 0;
}

static void e1000_write_reg(uint16_t reg, uint32_t value) {
    if (e1000_mmio) {
        e1000_mmio[reg / 4] = value;
    }
}

static void e1000_eeprom_read(uint16_t addr, uint16_t *data) {
    e1000_write_reg(E1000_REG_EERD, (addr << 8) | 1);
    while (!(e1000_read_reg(E1000_REG_EERD) & (1 << 4)));
    *data = (uint16_t)(e1000_read_reg(E1000_REG_EERD) >> 16);
}

void e1000_send(net_device_t *dev, uint8_t *data, uint32_t len) {
    (void)dev;
    if (!e1000_mmio || !tx_descs) return;
    
    static uint32_t tx_index = 0;
    e1000_tx_desc_t *desc = &tx_descs[tx_index];
    
    // Copy data to buffer
    uint8_t *tx_buffer = (uint8_t *)(uintptr_t)desc->addr;
    for (uint32_t i = 0; i < len && i < 1518; i++) {
        tx_buffer[i] = data[i];
    }
    
    desc->length = len;
    desc->cmd = 0x0B; // EOP, RS
    desc->status = 0;
    
    // Advance TX descriptor
    tx_index = (tx_index + 1) % E1000_TX_DESC_COUNT;
    e1000_write_reg(E1000_REG_TDT, tx_index);
    
    // Wait for transmission to complete
    while (!(desc->status & 0x01));
}

void e1000_recv(net_device_t *dev, void (*callback)(uint8_t *data, uint32_t len)) {
    (void)dev;
    if (!e1000_mmio || !rx_descs || !callback) return;
    
    uint32_t rdt = e1000_read_reg(E1000_REG_RDT);
    uint32_t rdh = e1000_read_reg(E1000_REG_RDH);
    
    while (rdt != rdh) {
        e1000_rx_desc_t *desc = &rx_descs[rdt];
        
        if (desc->status & 0x01) {
            // Packet received
            uint8_t *rx_buffer = (uint8_t *)(uintptr_t)desc->addr;
            callback(rx_buffer, desc->length);
            
            // Reset descriptor
            desc->status = 0;
        }
        
        rdt = (rdt + 1) % E1000_RX_DESC_COUNT;
        e1000_write_reg(E1000_REG_RDT, rdt);
        rdh = e1000_read_reg(E1000_REG_RDH);
    }
}

static void e1000_name(void) {
    e1000_dev.name[0] = 'e';
    e1000_dev.name[1] = '1';
    e1000_dev.name[2] = '0';
    e1000_dev.name[3] = '0';
    e1000_dev.name[4] = '0';
    e1000_dev.name[5] = '\0';
}

static int e1000_find(PciAddress* out) {
    static const uint16_t ids[] = {
        0x100E, 0x100F, 0x1010, 0x1011, 0x1012, 0x1013, 0x1015, 0x1016,
        0x1017, 0x1018, 0x1019, 0x101A, 0x101D, 0x1026, 0x1027, 0x1028,
        0x1075, 0x1076, 0x1077, 0x1078, 0x1079, 0x107A, 0x107B, 0x107C,
        0x108A, 0x1096, 0x10A4, 0x10A5, 0x10B5, 0x10D3, 0x10EA, 0x10F6
    };
    for (uint32_t i = 0; i < sizeof(ids) / sizeof(ids[0]); i++) {
        if (pci_find_device(0x8086, ids[i], out) == 0)
            return 0;
    }
    return -1;
}

void e1000_init(void) {
    PciAddress addr;
    if (e1000_find(&addr) != 0) {
        kprint("E1000: no supported PCI device found\n");
        return;
    }

    kprint("Initializing E1000 network driver...\n");
    uint32_t cmdsts = pci_read32(addr, 0x04);
    pci_write32(addr, 0x04, cmdsts | (1u << 1) | (1u << 2));
    e1000_mmio = (volatile uint32_t*)(uintptr_t)pci_bar(addr, 0);
    if (!e1000_mmio) {
        kprint("E1000: invalid BAR0\n");
        return;
    }
    
    // Allocate memory for descriptors and buffers
    tx_descs = (e1000_tx_desc_t *)kalloc(sizeof(e1000_tx_desc_t) * E1000_TX_DESC_COUNT);
    rx_descs = (e1000_rx_desc_t *)kalloc(sizeof(e1000_rx_desc_t) * E1000_RX_DESC_COUNT);
    rx_buffers = (uint8_t *)kalloc(2048 * E1000_RX_DESC_COUNT);
    tx_buffers = (uint8_t *)kalloc(2048 * E1000_TX_DESC_COUNT);
    
    if (!tx_descs || !rx_descs || !rx_buffers || !tx_buffers) {
        kprint("E1000: Memory allocation failed\n");
        return;
    }
    
    // Initialize RX descriptors
    for (uint32_t i = 0; i < E1000_RX_DESC_COUNT; i++) {
        rx_descs[i].addr = (uint64_t)&rx_buffers[i * 2048];
        rx_descs[i].status = 0;
    }
    
    // Initialize TX descriptors
    for (uint32_t i = 0; i < E1000_TX_DESC_COUNT; i++) {
        tx_descs[i].addr = (uint64_t)&tx_buffers[i * 2048];
        tx_descs[i].status = 1;
    }

    e1000_write_reg(E1000_REG_IMS, 0);
    e1000_write_reg(E1000_REG_RDBAL, (uint32_t)(uint64_t)rx_descs);
    e1000_write_reg(E1000_REG_RDBAH, (uint32_t)((uint64_t)rx_descs >> 32));
    e1000_write_reg(E1000_REG_RDLEN, sizeof(e1000_rx_desc_t) * E1000_RX_DESC_COUNT);
    e1000_write_reg(E1000_REG_RDH, 0);
    e1000_write_reg(E1000_REG_RDT, E1000_RX_DESC_COUNT - 1);
    e1000_write_reg(E1000_REG_TDBAL, (uint32_t)(uint64_t)tx_descs);
    e1000_write_reg(E1000_REG_TDBAH, (uint32_t)((uint64_t)tx_descs >> 32));
    e1000_write_reg(E1000_REG_TDLEN, sizeof(e1000_tx_desc_t) * E1000_TX_DESC_COUNT);
    e1000_write_reg(E1000_REG_TDH, 0);
    e1000_write_reg(E1000_REG_TDT, 0);

    e1000_dev.ip = 0;
    e1000_dev.send = e1000_send;
    e1000_dev.recv = e1000_recv;
    e1000_dev.next = NULL;
    e1000_name();

    uint32_t ral = e1000_read_reg(E1000_REG_RAL);
    uint32_t rah = e1000_read_reg(E1000_REG_RAH);
    e1000_dev.mac[0] = (uint8_t)ral;
    e1000_dev.mac[1] = (uint8_t)(ral >> 8);
    e1000_dev.mac[2] = (uint8_t)(ral >> 16);
    e1000_dev.mac[3] = (uint8_t)(ral >> 24);
    e1000_dev.mac[4] = (uint8_t)rah;
    e1000_dev.mac[5] = (uint8_t)(rah >> 8);

    e1000_write_reg(E1000_REG_RCTL, E1000_RCTL_EN | E1000_RCTL_BAM | E1000_RCTL_SZ_2048 | E1000_RCTL_SECRC);
    e1000_write_reg(E1000_REG_TCTL, E1000_TCTL_EN | E1000_TCTL_PSP | (0x10u << E1000_TCTL_COLD_SHIFT));
    
    net_register_device(&e1000_dev);
    kprint("E1000 driver initialized\n");
}

net_device_t *e1000_get_device(void) {
    return &e1000_dev;
}
