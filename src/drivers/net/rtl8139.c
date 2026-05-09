#include "rtl8139.h"
#include "net.h"
#include "drivers/bus/io.h"
#include "drivers/bus/pci.h"
#include "freelib/kalloc.h"
#include "freelib/kstdio.h"
#include "freelib/kstdint.h"

#define RTL8139_VENDOR 0x10EC
#define RTL8139_DEVICE 0x8139

#define RTL_IDR0     0x00
#define RTL_TXADDR0  0x20
#define RTL_TXSTAT0  0x10
#define RTL_RBSTART  0x30
#define RTL_CR       0x37
#define RTL_CAPR     0x38
#define RTL_CBR      0x3A
#define RTL_IMR      0x3C
#define RTL_ISR      0x3E
#define RTL_TCR      0x40
#define RTL_RCR      0x44
#define RTL_CONFIG1  0x52

#define RTL_RX_BUF_SIZE (8192 + 16 + 1500)

static uint16_t rtl_io;
static uint8_t* rtl_rx_buf;
static uint8_t* rtl_tx_buf[4];
static uint32_t rtl_tx_cur;
static uint32_t rtl_rx_cur;
static net_device_t rtl_dev;

static uint8_t rtl_read8(uint16_t reg) { return inb(rtl_io + reg); }
static uint16_t rtl_read16(uint16_t reg) { return inw(rtl_io + reg); }
static uint32_t rtl_read32(uint16_t reg) { return inl(rtl_io + reg); }
static void rtl_write8(uint16_t reg, uint8_t v) { outb(rtl_io + reg, v); }
static void rtl_write16(uint16_t reg, uint16_t v) { outw(rtl_io + reg, v); }
static void rtl_write32(uint16_t reg, uint32_t v) { outl(rtl_io + reg, v); }

static void rtl_name(void) {
    rtl_dev.name[0] = 'r';
    rtl_dev.name[1] = 't';
    rtl_dev.name[2] = 'l';
    rtl_dev.name[3] = '8';
    rtl_dev.name[4] = '1';
    rtl_dev.name[5] = '3';
    rtl_dev.name[6] = '9';
    rtl_dev.name[7] = '\0';
}

static void rtl8139_send(net_device_t* dev, uint8_t* data, uint32_t len) {
    (void)dev;
    if (!rtl_io || len > 1518)
        return;
    uint32_t tx = rtl_tx_cur & 3u;
    for (uint32_t i = 0; i < len; i++)
        rtl_tx_buf[tx][i] = data[i];
    rtl_write32(RTL_TXADDR0 + tx * 4u, (uint32_t)(uint64_t)rtl_tx_buf[tx]);
    rtl_write32(RTL_TXSTAT0 + tx * 4u, len);
    rtl_tx_cur++;
}

static void rtl8139_recv(net_device_t* dev, void (*callback)(uint8_t* data, uint32_t len)) {
    (void)dev;
    if (!rtl_io || !callback)
        return;

    while ((rtl_read8(RTL_CR) & 1u) == 0) {
        uint32_t offset = rtl_rx_cur % 8192u;
        uint8_t* pkt = rtl_rx_buf + offset;
        uint16_t status = *(uint16_t*)pkt;
        uint16_t len = *(uint16_t*)(pkt + 2);
        if ((status & 1u) && len >= 4)
            callback(pkt + 4, len - 4);
        rtl_rx_cur = (rtl_rx_cur + len + 4u + 3u) & ~3u;
        rtl_write16(RTL_CAPR, (uint16_t)(rtl_rx_cur - 16u));
    }
    rtl_write16(RTL_ISR, rtl_read16(RTL_ISR));
}

void rtl8139_init(void) {
    PciAddress addr;
    if (pci_find_device(RTL8139_VENDOR, RTL8139_DEVICE, &addr) != 0) {
        kprint("RTL8139: no PCI device found\n");
        return;
    }

    uint64_t bar = pci_bar(addr, 0);
    if ((bar & 1u) == 0) {
        kprint("RTL8139: MMIO BAR detected; PIO path unavailable\n");
        return;
    }
    rtl_io = (uint16_t)(bar & ~3u);

    uint32_t cmdsts = pci_read32(addr, 0x04);
    pci_write32(addr, 0x04, cmdsts | (1u << 0) | (1u << 2));

    rtl_rx_buf = (uint8_t*)kalloc(RTL_RX_BUF_SIZE);
    for (uint32_t i = 0; i < 4; i++)
        rtl_tx_buf[i] = (uint8_t*)kalloc(2048);
    if (!rtl_rx_buf || !rtl_tx_buf[0] || !rtl_tx_buf[1] || !rtl_tx_buf[2] || !rtl_tx_buf[3]) {
        kprint("RTL8139: allocation failed\n");
        return;
    }

    rtl_write8(RTL_CONFIG1, 0);
    rtl_write8(RTL_CR, 0x10);
    for (uint32_t spin = 0; spin < 100000; spin++) {
        if ((rtl_read8(RTL_CR) & 0x10u) == 0)
            break;
    }

    rtl_name();
    rtl_dev.ip = 0;
    rtl_dev.send = rtl8139_send;
    rtl_dev.recv = rtl8139_recv;
    for (uint32_t i = 0; i < ETH_ALEN; i++)
        rtl_dev.mac[i] = rtl_read8(RTL_IDR0 + i);

    rtl_write32(RTL_RBSTART, (uint32_t)(uint64_t)rtl_rx_buf);
    rtl_write16(RTL_IMR, 0);
    rtl_write32(RTL_RCR, 0x00000F | (1u << 7));
    rtl_write32(RTL_TCR, 0x03000000);
    rtl_write8(RTL_CR, 0x0C);

    net_register_device(&rtl_dev);
    kprint("RTL8139 driver initialized\n");
}
