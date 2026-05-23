#include "rtl8169.h"
#include "net.h"
#include "drivers/bus/io.h"
#include "drivers/bus/pci.h"
#include "freelib/kalloc.h"
#include "freelib/kstdio.h"
#include "freelib/kstdint.h"

#define RTL8169_DESC_COUNT 64
#define RTL8169_BUFFER_SIZE 2048

#define RTL8169_REG_MAC0   0x00
#define RTL8169_REG_MAC4   0x04
#define RTL8169_REG_TXSTART 0x20
#define RTL8169_REG_TXCONF  0x10
#define RTL8169_REG_RXSTART 0xE4
#define RTL8169_REG_RXCONF  0x44
#define RTL8169_REG_CMD     0x37
#define RTL8169_REG_INTR_MASK 0x3C
#define RTL8169_REG_INTR_STATUS 0x3E
#define RTL8169_REG_TXPOLL  0x38
#define RTL8169_REG_IMR     0x3C
#define RTL8169_REG_ISR     0x3E
#define RTL8169_REG_CONFIG1 0x52
#define RTL8169_REG_CONFIG2 0x53
#define RTL8169_REG_CONFIG3 0x54
#define RTL8169_REG_CONFIG4 0x55
#define RTL8169_REG_PHYSTATUS 0x6C
#define RTL8169_REG_GPIO    0xD0
#define RTL8169_REG_CPLUS1  0xE0
#define RTL8169_REG_CPLUS2  0xE2
#define RTL8169_REG_TXDESC0 0x20
#define RTL8169_REG_TXDESC1 0x28
#define RTL8169_REG_TXDESC2 0x30
#define RTL8169_REG_TXDESC3 0x38
#define RTL8169_REG_RXDESC0 0xE4
#define RTL8169_REG_RXDESC1 0xE8
#define RTL8169_REG_RXDESC2 0xEC
#define RTL8169_REG_RXDESC3 0xF0
#define RTL8169_REG_EECMD   0x50
#define RTL8169_REG_CFG9346 0x50
#define RTL8169_REG_CHIPCMD 0x37

#define RTL8169_CMD_TX_ENABLE  0x04
#define RTL8169_CMD_RX_ENABLE  0x08
#define RTL8169_CMD_RESET      0x10

#define RTL8169_TXCFG_HW_VER  0x7C000000
#define RTL8169_TXCFG_MAX_DMA 0x00000700

#define RTL8169_RXCFG_MAX_DMA 0x00000700
#define RTL8169_RXCFG_1024    0x00030000
#define RTL8169_RXCFG_MX_DMA_UNLIMITED 0x00000000

#define RTL8169_INTR_RX_OK    0x0001
#define RTL8169_INTR_TX_OK    0x0002
#define RTL8169_INTR_RX_ERR   0x0004
#define RTL8169_INTR_TX_ERR   0x0008
#define RTL8169_INTR_LINK_CHG 0x0020
#define RTL8169_INTR_SYS_ERR  0x4000

#define RTL8169_FLAG_OWN  (1u << 31)
#define RTL8169_FLAG_EOR  (1u << 30)
#define RTL8169_FLAG_FS   (1u << 29)
#define RTL8169_FLAG_LS   (1u << 28)

typedef struct {
    uint32_t addr_low;
    uint32_t addr_high;
    uint32_t opts1;
    uint32_t opts2;
} __attribute__((packed)) rtl8169_desc_t;

static volatile uint32_t *rtl_mmio = NULL;
static rtl8169_desc_t *tx_ring = NULL;
static rtl8169_desc_t *rx_ring = NULL;
static uint8_t *tx_bufs = NULL;
static uint8_t *rx_bufs = NULL;
static uint32_t tx_cur;
static uint32_t rx_cur;
static net_device_t rtl8169_dev;

static uint32_t rtl_read32(uint16_t reg) {
    if (rtl_mmio) return rtl_mmio[reg / 4];
    return 0;
}

static void rtl_write32(uint16_t reg, uint32_t val) {
    if (rtl_mmio) rtl_mmio[reg / 4] = val;
}

static uint16_t rtl_read16(uint16_t reg) {
    if (rtl_mmio) return (uint16_t)(rtl_mmio[reg / 4] & 0xFFFF);
    return 0;
}

static void rtl_write16(uint16_t reg, uint16_t val) {
    if (rtl_mmio) {
        volatile uint32_t *p = &rtl_mmio[reg / 4];
        uint32_t v = *p;
        v = (v & ~0xFFFFu) | val;
        *p = v;
    }
}

static uint8_t rtl_read8(uint16_t reg) {
    if (rtl_mmio) return (uint8_t)(rtl_mmio[reg / 4] & 0xFF);
    return 0;
}

static void rtl_write8(uint16_t reg, uint8_t val) {
    if (rtl_mmio) {
        volatile uint32_t *p = &rtl_mmio[reg / 4];
        uint32_t v = *p;
        v = (v & ~0xFFu) | val;
        *p = v;
    }
}

static void rtl_name(void) {
    rtl8169_dev.name[0] = 'r';
    rtl8169_dev.name[1] = 't';
    rtl8169_dev.name[2] = 'l';
    rtl8169_dev.name[3] = '8';
    rtl8169_dev.name[4] = '1';
    rtl8169_dev.name[5] = '6';
    rtl8169_dev.name[6] = '9';
    rtl8169_dev.name[7] = '\0';
}

static void rtl8169_send(net_device_t *dev, uint8_t *data, uint32_t len) {
    (void)dev;
    if (!rtl_mmio || !tx_ring || len > RTL8169_BUFFER_SIZE) return;

    uint32_t idx = tx_cur % RTL8169_DESC_COUNT;
    uint32_t buf_off = idx * RTL8169_BUFFER_SIZE;

    for (uint32_t i = 0; i < len; i++)
        tx_bufs[buf_off + i] = data[i];

    tx_ring[idx].addr_low = (uint32_t)(uint64_t)&tx_bufs[buf_off];
    tx_ring[idx].addr_high = (uint32_t)((uint64_t)&tx_bufs[buf_off] >> 32);
    tx_ring[idx].opts1 = (len & 0xFFFF) | RTL8169_FLAG_OWN | RTL8169_FLAG_FS | RTL8169_FLAG_LS;
    tx_ring[idx].opts2 = 0;

    __asm__ volatile("" ::: "memory");

    rtl_write32(RTL8169_REG_TXPOLL, 0x40);
    tx_cur++;
}

static void rtl8169_recv(net_device_t *dev, void (*callback)(uint8_t *data, uint32_t len)) {
    (void)dev;
    if (!rtl_mmio || !rx_ring || !callback) return;

    for (uint32_t i = 0; i < RTL8169_DESC_COUNT; i++) {
        uint32_t idx = (rx_cur + i) % RTL8169_DESC_COUNT;
        if ((rx_ring[idx].opts1 & RTL8169_FLAG_OWN) == 0)
            break;

        uint32_t pkt_len = rx_ring[idx].opts1 & 0xFFFF;
        if (pkt_len > 4 && pkt_len <= RTL8169_BUFFER_SIZE) {
            uint32_t buf_off = idx * RTL8169_BUFFER_SIZE;
            callback(&rx_bufs[buf_off], pkt_len);
        }

        rx_ring[idx].opts1 = RTL8169_FLAG_OWN | RTL8169_BUFFER_SIZE;
        rx_cur = (rx_cur + 1) % RTL8169_DESC_COUNT;
    }

    rtl_write16(RTL8169_REG_INTR_STATUS, 0xFFFF);
}

static int rtl8169_find(PciAddress *out) {
    if (pci_find_device(0x10EC, 0x8168, out) == 0) return 0;
    if (pci_find_device(0x10EC, 0x8169, out) == 0) return 0;
    if (pci_find_device(0x10EC, 0x8125, out) == 0) return 0;
    return -1;
}

void rtl8169_init(void) {
    PciAddress addr;
    if (rtl8169_find(&addr) != 0) {
        kprint("RTL8169: no PCI device found\n");
        return;
    }

    kprint("Initializing RTL8169 network driver...\n");

    uint32_t cmdsts = pci_read32(addr, 0x04);
    pci_write32(addr, 0x04, cmdsts | (1u << 1) | (1u << 2));

    uint64_t bar = pci_bar(addr, 0);
    if (!bar || (bar & 1u)) {
        kprint("RTL8169: invalid or I/O BAR\n");
        return;
    }
    rtl_mmio = (volatile uint32_t *)(uintptr_t)(bar & ~0xFull);

    rtl_write8(RTL8169_REG_CMD, RTL8169_CMD_RESET);
    for (uint32_t spin = 0; spin < 100000; spin++) {
        if ((rtl_read8(RTL8169_REG_CMD) & RTL8169_CMD_RESET) == 0)
            break;
    }

    tx_ring = (rtl8169_desc_t *)kalloc(sizeof(rtl8169_desc_t) * RTL8169_DESC_COUNT);
    rx_ring = (rtl8169_desc_t *)kalloc(sizeof(rtl8169_desc_t) * RTL8169_DESC_COUNT);
    tx_bufs = (uint8_t *)kalloc(RTL8169_BUFFER_SIZE * RTL8169_DESC_COUNT);
    rx_bufs = (uint8_t *)kalloc(RTL8169_BUFFER_SIZE * RTL8169_DESC_COUNT);

    if (!tx_ring || !rx_ring || !tx_bufs || !rx_bufs) {
        kprint("RTL8169: allocation failed\n");
        if (tx_ring) kfree(tx_ring);
        if (rx_ring) kfree(rx_ring);
        if (tx_bufs) kfree(tx_bufs);
        if (rx_bufs) kfree(rx_bufs);
        tx_ring = NULL; rx_ring = NULL; tx_bufs = NULL; rx_bufs = NULL;
        return;
    }

    for (uint32_t i = 0; i < RTL8169_DESC_COUNT; i++) {
        tx_ring[i].addr_low = 0;
        tx_ring[i].addr_high = 0;
        tx_ring[i].opts1 = 0;
        tx_ring[i].opts2 = 0;

        rx_ring[i].addr_low = (uint32_t)(uint64_t)&rx_bufs[i * RTL8169_BUFFER_SIZE];
        rx_ring[i].addr_high = (uint32_t)((uint64_t)&rx_bufs[i * RTL8169_BUFFER_SIZE] >> 32);
        rx_ring[i].opts1 = RTL8169_FLAG_OWN | RTL8169_BUFFER_SIZE;
        rx_ring[i].opts2 = 0;
    }

    rtl_write32(RTL8169_REG_TXDESC0, (uint32_t)(uint64_t)tx_ring);
    rtl_write32(RTL8169_REG_TXDESC1, (uint32_t)((uint64_t)tx_ring >> 32));
    rtl_write32(RTL8169_REG_RXDESC0, (uint32_t)(uint64_t)rx_ring);
    rtl_write32(RTL8169_REG_RXDESC1, (uint32_t)((uint64_t)rx_ring >> 32));

    rtl_write16(RTL8169_REG_INTR_MASK, 0);
    rtl_write16(RTL8169_REG_INTR_STATUS, 0xFFFF);

    rtl_write32(RTL8169_REG_TXCONF, RTL8169_TXCFG_MAX_DMA);
    rtl_write32(RTL8169_REG_RXCONF, RTL8169_RXCFG_1024 | RTL8169_RXCFG_MX_DMA_UNLIMITED);

    rtl_write8(RTL8169_REG_CMD, RTL8169_CMD_TX_ENABLE | RTL8169_CMD_RX_ENABLE);

    rtl_name();
    rtl8169_dev.ip = 0;
    rtl8169_dev.send = rtl8169_send;
    rtl8169_dev.recv = rtl8169_recv;
    rtl8169_dev.next = NULL;

    for (uint32_t i = 0; i < ETH_ALEN; i++)
        rtl8169_dev.mac[i] = rtl_read8(RTL8169_REG_MAC0 + i);

    net_register_device(&rtl8169_dev);
    kprint("RTL8169 driver initialized\n");
}

net_device_t *rtl8169_get_device(void) {
    return &rtl8169_dev;
}
