#include "virtio_net.h"
#include "net.h"
#include "drivers/bus/pci.h"
#include "freelib/kalloc.h"
#include "freelib/kstdio.h"
#include "freelib/kstdint.h"

#define VIRTIO_NET_QUEUE_SIZE 128
#define VIRTIO_BUFFER_SIZE 2048

#define VIRTIO_PCI_CAP_COMMON_CFG 1
#define VIRTIO_PCI_CAP_NOTIFY_CFG 2
#define VIRTIO_PCI_CAP_DEVICE_CFG 4

#define VIRTIO_STATUS_ACK      1
#define VIRTIO_STATUS_DRIVER   2
#define VIRTIO_STATUS_DRIVER_OK 4
#define VIRTIO_STATUS_FEATURES_OK 8

#define VIRTIO_F_VERSION_1 (1ull << 32)

#define VIRTIO_NET_F_MAC   (1ull << 5)

#define VRING_DESC_F_NEXT  1
#define VRING_DESC_F_WRITE 2

typedef struct {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} __attribute__((packed)) vring_desc;

typedef struct {
    uint32_t flags;
    uint16_t idx;
    uint16_t ring[VIRTIO_NET_QUEUE_SIZE];
} __attribute__((packed)) vring_avail;

typedef struct {
    uint32_t id;
    uint32_t len;
} __attribute__((packed)) vring_used_elem;

typedef struct {
    uint16_t flags;
    uint16_t idx;
    vring_used_elem ring[VIRTIO_NET_QUEUE_SIZE];
} __attribute__((packed)) vring_used;

typedef struct {
    vring_desc desc[VIRTIO_NET_QUEUE_SIZE];
    vring_avail avail;
    vring_used used;
} __attribute__((packed)) virtqueue;

typedef struct {
    uint32_t device_feature;
    uint32_t driver_feature;
    uint32_t num_queues;
    uint8_t device_status;
    uint8_t config_generation;
    uint16_t queue_select;
    uint16_t queue_size;
    uint16_t queue_msix;
    uint32_t queue_desc_lo;
    uint32_t queue_desc_hi;
    uint32_t queue_driver_lo;
    uint32_t queue_driver_hi;
    uint32_t queue_device_lo;
    uint32_t queue_device_hi;
    uint16_t queue_notify_off;
    uint16_t queue_enable;
} __attribute__((packed)) virtio_pci_common_t;

typedef struct {
    uint8_t mac[6];
    uint16_t status;
} __attribute__((packed)) virtio_net_cfg_t;

static volatile uint32_t *virtio_mmio = NULL;
static volatile virtio_pci_common_t *common = NULL;
static volatile uint8_t *notify = NULL;
static volatile virtio_net_cfg_t *net_cfg = NULL;
static uint32_t notify_off_multiplier;

static virtqueue *tx_vq = NULL;
static virtqueue *rx_vq = NULL;
static uint8_t *tx_bufs = NULL;
static uint8_t *rx_bufs = NULL;
static uint16_t tx_avail_idx;
static uint16_t rx_avail_idx;
static net_device_t virtio_dev;

static uint32_t virtio_read32(uint16_t reg) {
    if (virtio_mmio) return virtio_mmio[reg / 4];
    return 0;
}

static void virtio_write32(uint16_t reg, uint32_t val) {
    if (virtio_mmio) virtio_mmio[reg / 4] = val;
}

static void virtio_dev_name(void) {
    virtio_dev.name[0] = 'v';
    virtio_dev.name[1] = 'i';
    virtio_dev.name[2] = 'r';
    virtio_dev.name[3] = 't';
    virtio_dev.name[4] = 'i';
    virtio_dev.name[5] = 'o';
    virtio_dev.name[6] = '\0';
}

static void virtqueue_notify(virtqueue *vq, uint16_t idx) {
    uint32_t off = (uint32_t)notify_off_multiplier * idx;
    *(volatile uint16_t *)(notify + off) = idx;
}

static void virtio_net_send(net_device_t *dev, uint8_t *data, uint32_t len) {
    (void)dev;
    if (!tx_vq || !tx_bufs || len > VIRTIO_BUFFER_SIZE) return;

    uint16_t idx = tx_vq->avail.idx & (VIRTIO_NET_QUEUE_SIZE - 1);
    uint32_t buf_off = idx * VIRTIO_BUFFER_SIZE;

    for (uint32_t i = 0; i < len; i++)
        tx_bufs[buf_off + i] = data[i];

    tx_vq->desc[idx].addr = (uint64_t)&tx_bufs[buf_off];
    tx_vq->desc[idx].len = len;
    tx_vq->desc[idx].flags = 0;
    tx_vq->desc[idx].next = 0;

    __asm__ volatile("" ::: "memory");
    tx_vq->avail.ring[tx_vq->avail.idx % VIRTIO_NET_QUEUE_SIZE] = idx;
    __asm__ volatile("" ::: "memory");
    tx_vq->avail.idx++;
    __asm__ volatile("" ::: "memory");

    virtqueue_notify(tx_vq, 0);
}

static void virtio_net_recv(net_device_t *dev, void (*callback)(uint8_t *data, uint32_t len)) {
    (void)dev;
    if (!rx_vq || !rx_bufs || !callback) return;

    while (rx_vq->used.idx != rx_avail_idx) {
        uint16_t used_idx = rx_avail_idx % VIRTIO_NET_QUEUE_SIZE;
        uint16_t desc_idx = (uint16_t)rx_vq->used.ring[used_idx].id;
        uint32_t pkt_len = rx_vq->used.ring[used_idx].len;

        if (pkt_len > 0 && pkt_len <= VIRTIO_BUFFER_SIZE) {
            uint32_t buf_off = desc_idx * VIRTIO_BUFFER_SIZE;
            callback(&rx_bufs[buf_off], (uint32_t)pkt_len);
        }

        rx_vq->desc[desc_idx].addr = (uint64_t)&rx_bufs[desc_idx * VIRTIO_BUFFER_SIZE];
        rx_vq->desc[desc_idx].len = VIRTIO_BUFFER_SIZE;
        rx_vq->desc[desc_idx].flags = VRING_DESC_F_WRITE;
        rx_vq->desc[desc_idx].next = 0;

        __asm__ volatile("" ::: "memory");
        rx_vq->avail.ring[rx_vq->avail.idx % VIRTIO_NET_QUEUE_SIZE] = desc_idx;
        __asm__ volatile("" ::: "memory");
        rx_vq->avail.idx++;

        rx_avail_idx = (rx_avail_idx + 1) & 0xFFFF;
    }
}

static int virtio_net_find_pci(PciAddress *out) {
    if (pci_find_device(0x1AF4, 0x1000, out) == 0) return 0;
    if (pci_find_device(0x1AF4, 0x1041, out) == 0) return 0;
    return -1;
}

void virtio_net_init(void) {
    PciAddress addr;
    if (virtio_net_find_pci(&addr) != 0) {
        kprint("virtio-net: no PCI device found\n");
        return;
    }

    kprint("Initializing virtio-net driver...\n");

    uint32_t cmdsts = pci_read32(addr, 0x04);
    pci_write32(addr, 0x04, cmdsts | (1u << 1) | (1u << 2));

    uint64_t bar0 = pci_bar(addr, 0);
    if (!bar0) {
        kprint("virtio-net: no valid BAR0\n");
        return;
    }
    virtio_mmio = (volatile uint32_t *)(uintptr_t)(bar0 & ~0xFull);

    uint32_t cap_list = virtio_read32(0x34);
    uint32_t common_off = 0;
    uint32_t notify_off = 0;
    uint32_t notify_off_multiplier_val = 0;
    uint32_t device_off = 0;

    if (cap_list & 0xFF) {
        uint32_t cap_ptr = cap_list & 0xFC;
        while (cap_ptr) {
            uint8_t cap_vndr = (uint8_t)(virtio_read32(cap_ptr) & 0xFF);
            uint8_t cap_next = (uint8_t)((virtio_read32(cap_ptr) >> 8) & 0xFF);
            uint8_t cap_type = (uint8_t)((virtio_read32(cap_ptr) >> 16) & 0xFF);
            uint32_t cap_bar_off = virtio_read32(cap_ptr + 4);

            if (cap_vndr == 0x09) {
                uint32_t cap_info = virtio_read32(cap_ptr + 8);
                uint32_t off = cap_bar_off & ~0xFull;

                if (cap_type == VIRTIO_PCI_CAP_COMMON_CFG)
                    common_off = off;
                else if (cap_type == VIRTIO_PCI_CAP_NOTIFY_CFG) {
                    notify_off = off;
                    notify_off_multiplier_val = (cap_info >> 16) & 0xFFFF;
                } else if (cap_type == VIRTIO_PCI_CAP_DEVICE_CFG)
                    device_off = off;
            }
            cap_ptr = cap_next ? (cap_ptr + cap_next) : 0;
        }
    }

    if (!common_off) {
        kprint("virtio-net: no common cfg\n");
        virtio_mmio = NULL;
        return;
    }

    notify_off_multiplier = notify_off_multiplier_val;
    common = (volatile virtio_pci_common_t *)((uintptr_t)virtio_mmio + common_off);
    notify = (volatile uint8_t *)((uintptr_t)virtio_mmio + notify_off);

    common->device_status = VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER;

    uint64_t dev_features = (uint64_t)common->device_feature;
    common->driver_feature = (uint32_t)(dev_features & VIRTIO_F_VERSION_1);
    common->device_status = VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_FEATURES_OK;

    if ((common->device_status & VIRTIO_STATUS_FEATURES_OK) == 0) {
        kprint("virtio-net: feature negotiation failed\n");
        return;
    }

    if (device_off)
        net_cfg = (volatile virtio_net_cfg_t *)((uintptr_t)virtio_mmio + device_off);

    tx_vq = (virtqueue *)kalloc(sizeof(virtqueue));
    rx_vq = (virtqueue *)kalloc(sizeof(virtqueue));
    tx_bufs = (uint8_t *)kalloc(VIRTIO_BUFFER_SIZE * VIRTIO_NET_QUEUE_SIZE);
    rx_bufs = (uint8_t *)kalloc(VIRTIO_BUFFER_SIZE * VIRTIO_NET_QUEUE_SIZE);

    if (!tx_vq || !rx_vq || !tx_bufs || !rx_bufs) {
        kprint("virtio-net: allocation failed\n");
        if (tx_vq) kfree(tx_vq);
        if (rx_vq) kfree(rx_vq);
        if (tx_bufs) kfree(tx_bufs);
        if (rx_bufs) kfree(rx_bufs);
        tx_vq = NULL; rx_vq = NULL; tx_bufs = NULL; rx_bufs = NULL;
        return;
    }

    for (uint32_t q = 0; q < 2; q++) {
        common->queue_select = (uint16_t)q;
        common->queue_size = VIRTIO_NET_QUEUE_SIZE;

        virtqueue *vq = (q == 0) ? tx_vq : rx_vq;
        for (uint32_t i = 0; i < VIRTIO_NET_QUEUE_SIZE; i++) {
            vq->desc[i].addr = 0;
            vq->desc[i].len = 0;
            vq->desc[i].flags = 0;
            vq->desc[i].next = 0;
            vq->avail.ring[i] = 0;
        }
        vq->avail.flags = 0;
        vq->avail.idx = 0;
        vq->used.flags = 0;
        vq->used.idx = 0;

        common->queue_desc_lo = (uint32_t)(uint64_t)vq->desc;
        common->queue_desc_hi = (uint32_t)((uint64_t)vq->desc >> 32);
        common->queue_driver_lo = (uint32_t)(uint64_t)&vq->avail;
        common->queue_driver_hi = (uint32_t)((uint64_t)&vq->avail >> 32);
        common->queue_device_lo = (uint32_t)(uint64_t)&vq->used;
        common->queue_device_hi = (uint32_t)((uint64_t)&vq->used >> 32);
        common->queue_enable = 1;
    }

    for (uint32_t i = 0; i < VIRTIO_NET_QUEUE_SIZE; i++) {
        rx_vq->desc[i].addr = (uint64_t)&rx_bufs[i * VIRTIO_BUFFER_SIZE];
        rx_vq->desc[i].len = VIRTIO_BUFFER_SIZE;
        rx_vq->desc[i].flags = VRING_DESC_F_WRITE;
        rx_vq->desc[i].next = 0;

        rx_vq->avail.ring[i] = (uint16_t)i;
    }
    __asm__ volatile("" ::: "memory");
    rx_vq->avail.idx = VIRTIO_NET_QUEUE_SIZE;
    rx_avail_idx = 0;
    tx_avail_idx = 0;

    common->device_status |= VIRTIO_STATUS_DRIVER_OK;

    virtio_dev_name();
    virtio_dev.ip = 0;
    virtio_dev.send = virtio_net_send;
    virtio_dev.recv = virtio_net_recv;
    virtio_dev.next = NULL;

    if (net_cfg) {
        for (uint32_t i = 0; i < ETH_ALEN; i++)
            virtio_dev.mac[i] = net_cfg->mac[i];
    } else {
        virtio_dev.mac[0] = 0x52;
        virtio_dev.mac[1] = 0x54;
        virtio_dev.mac[2] = 0x00;
        virtio_dev.mac[3] = 0x12;
        virtio_dev.mac[4] = 0x34;
        virtio_dev.mac[5] = 0x56;
    }

    net_register_device(&virtio_dev);
    kprint("virtio-net driver initialized\n");
}

net_device_t *virtio_net_get_device(void) {
    return &virtio_dev;
}
