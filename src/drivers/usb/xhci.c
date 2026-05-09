#include "drivers/usb/xhci.h"
#include "drivers/bus/pci.h"
#include "freelib/kstdio.h"
#include "freelib/kstdint.h"
#include "mm/vm.h"

#define USBCMD_RS   (1u << 0)
#define USBCMD_HCRST (1u << 1)
#define USBCMD_INTE (1u << 2)

#define USBSTS_HCH (1u << 11)
#define USBSTS_CNR (1u << 12)

#define XHCI_EXT_PROTOCOL 2u
#define XHCI_TRB_TYPE_LINK 6u
#define XHCI_TRB_TYPE_ENABLE_SLOT 9u
#define XHCI_TRB_TYPE_ADDRESS_DEVICE 11u
#define XHCI_TRB_TYPE_CMD_COMPLETION 33u
#define XHCI_TRB_TYPE_TRANSFER_EVENT 32u
#define XHCI_TRB_CYCLE 1u
#define XHCI_TRB_IOC (1u << 5)

#define XHCI_CMD_RING_TRBS 256u
#define XHCI_EVENT_RING_TRBS 256u
#define XHCI_MAX_SLOTS 64u

typedef struct {
    uint64_t parameter;
    uint32_t status;
    uint32_t control;
} __attribute__((packed)) XhciTrb;

typedef struct {
    uint32_t route_string;
    uint32_t speed_entries;
    uint32_t tt_info;
    uint32_t state;
    uint32_t reserved[4];
} __attribute__((packed)) XhciSlotContext;

typedef struct {
    uint64_t dequeue;
    uint32_t ep_info;
    uint32_t ep_info2;
    uint32_t reserved[4];
} __attribute__((packed)) XhciEndpointContext;

typedef struct {
    uint32_t drop_context_flags;
    uint32_t add_context_flags;
    uint32_t reserved[5];
    uint32_t config_value;
    XhciSlotContext slot;
    XhciEndpointContext ep0;
} __attribute__((packed)) XhciInputContext32;

typedef struct {
    uint32_t ring_segment_base_lo;
    uint32_t ring_segment_base_hi;
    uint32_t ring_segment_size;
    uint32_t reserved;
} __attribute__((packed)) XhciErstEntry;

static XhciTrb* cmd_ring;
static uint32_t cmd_index;
static uint32_t cmd_cycle;
static XhciTrb* event_ring;
static uint32_t event_index;
static uint32_t event_cycle;
static uint64_t* dcbaa;

static uint32_t mmio_read32(volatile uint32_t* r) {
    return *r;
}

static void mmio_write32(volatile uint32_t* r, uint32_t v) {
    *r = v;
}

static void mmio_write64(volatile uint32_t* r, uint64_t v) {
    r[0] = (uint32_t)v;
    r[1] = (uint32_t)(v >> 32);
}

static uint32_t trb_type(uint32_t control) {
    return (control >> 10) & 0x3Fu;
}

static void* xhci_alloc_pages(uint32_t pages) {
    void* first = NULL;
    for (uint32_t i = 0; i < pages; i++) {
        void* p = vm_alloc_page();
        if (!first)
            first = p;
    }
    return first;
}

static uint32_t xhci_page_count(uint32_t bytes) {
    return (bytes + VM_PAGE_SIZE - 1u) / VM_PAGE_SIZE;
}

static void xhci_zero(void* ptr, uint32_t bytes) {
    uint8_t* p = (uint8_t*)ptr;
    for (uint32_t i = 0; i < bytes; i++)
        p[i] = 0;
}

static void xhci_ctx_write32(uint8_t* ctx, uint32_t offset, uint32_t value) {
    *(uint32_t*)(ctx + offset) = value;
}

static void xhci_ctx_write64(uint8_t* ctx, uint32_t offset, uint64_t value) {
    *(uint64_t*)(ctx + offset) = value;
}

static const char* xhci_port_speed_name(uint32_t portsc) {
    uint32_t spd = (portsc >> 10) & 0xFu;
    switch (spd) {
        case 1: return "full";
        case 2: return "low";
        case 3: return "high";
        case 4: return "super";
        case 5: return "super_plus";
        default: return "unknown";
    }
}

static void xhci_print_protocol_caps(uint8_t* base, uint32_t hccparams) {
    uint32_t xecp = (hccparams >> 16) & 0xFFFFu;
    if (xecp == 0) {
        kprint("  xHCI: no extended capability list\n");
        return;
    }

    uint32_t off = xecp * 4u;
    for (;;) {
        volatile uint32_t* dw0p = (volatile uint32_t*)(base + off);
        uint32_t dw0 = mmio_read32(dw0p);
        uint8_t id = (uint8_t)(dw0 & 0xFFu);
        uint8_t next = (uint8_t)((dw0 >> 8) & 0xFFu);

        if (id == XHCI_EXT_PROTOCOL) {
            uint32_t dw1 = mmio_read32(dw0p + 1);
            uint32_t dw2 = mmio_read32(dw0p + 2);
            uint16_t rev = (uint16_t)(dw1 & 0xFFFFu);
            uint8_t major = (uint8_t)((rev >> 8) & 0xFFu);
            uint8_t minor = (uint8_t)(rev & 0xFFu);
            uint8_t poff = (uint8_t)(dw2 & 0xFFu);
            uint8_t pcnt = (uint8_t)((dw2 >> 8) & 0xFFu);

            kprint("  xHCI protocol: USB ");
            kprint_int(major);
            kprint(".");
            if (minor < 10)
                kprint("0");
            kprint_int(minor);
            kprint(" — host ports ");
            kprint_int(poff);
            kprint("–");
            kprint_int(poff + pcnt - 1);
            kprint("\n");
        }

        if (next == 0)
            break;
        off += (uint32_t)next * 4u;
    }
}

static int xhci_wait_cmd(volatile uint32_t* db, volatile uint32_t* ir, uint64_t expected_trb, uint32_t* slot_id) {
    *slot_id = 0;
    db[0] = 0;
    for (uint32_t spin = 0; spin < 4000000u; spin++) {
        XhciTrb* ev = &event_ring[event_index];
        if ((ev->control & XHCI_TRB_CYCLE) == event_cycle &&
            trb_type(ev->control) == XHCI_TRB_TYPE_CMD_COMPLETION) {
            uint64_t command = ev->parameter & ~0xFu;
            uint32_t code = (ev->status >> 24) & 0xFFu;
            *slot_id = (ev->control >> 24) & 0xFFu;
            event_index++;
            if (event_index == XHCI_EVENT_RING_TRBS) {
                event_index = 0;
                event_cycle ^= 1u;
            }
            mmio_write64(ir + (0x18u / 4u), (uint64_t)&event_ring[event_index]);
            return command == (expected_trb & ~0xFu) && code == 1u ? 0 : -1;
        }
    }
    return -1;
}

static int xhci_cmd(volatile uint32_t* db, volatile uint32_t* ir, uint64_t parameter, uint32_t status, uint32_t control, uint32_t* slot_id) {
    XhciTrb* trb = &cmd_ring[cmd_index];
    trb->parameter = parameter;
    trb->status = status;
    trb->control = control | (cmd_cycle ? XHCI_TRB_CYCLE : 0u);
    uint64_t expected = (uint64_t)trb;

    cmd_index++;
    if (cmd_index == XHCI_CMD_RING_TRBS - 1u) {
        cmd_ring[cmd_index].parameter = (uint64_t)&cmd_ring[0];
        cmd_ring[cmd_index].status = 0;
        cmd_ring[cmd_index].control = (XHCI_TRB_TYPE_LINK << 10) | (1u << 1) | (cmd_cycle ? XHCI_TRB_CYCLE : 0u);
        cmd_index = 0;
        cmd_cycle ^= 1u;
    }

    return xhci_wait_cmd(db, ir, expected, slot_id);
}

static uint32_t xhci_default_ep0_packet(uint32_t portsc) {
    uint32_t spd = (portsc >> 10) & 0xFu;
    if (spd >= 4)
        return 512;
    if (spd == 3)
        return 64;
    if (spd == 2)
        return 8;
    return 64;
}

static int xhci_address_port(volatile uint32_t* db, volatile uint32_t* ir, uint32_t port_id, uint32_t portsc, uint8_t context_size_64) {
    uint32_t slot_id = 0;
    if (xhci_cmd(db, ir, 0, 0, XHCI_TRB_TYPE_ENABLE_SLOT << 10, &slot_id) != 0 || slot_id == 0 || slot_id >= XHCI_MAX_SLOTS) {
        kprint("    xHCI: Enable Slot failed\n");
        return -1;
    }

    uint32_t ctx_size = context_size_64 ? 64u : 32u;
    uint32_t input_bytes = ctx_size * 3u;
    uint8_t* input = (uint8_t*)xhci_alloc_pages(xhci_page_count(input_bytes));
    void* output = xhci_alloc_pages(xhci_page_count(ctx_size * 32u));
    XhciTrb* ep0_ring = (XhciTrb*)xhci_alloc_pages(1);
    if (!input || !output || !ep0_ring)
        return -1;

    xhci_zero(input, input_bytes);
    xhci_zero(output, ctx_size * 32u);
    xhci_zero(ep0_ring, VM_PAGE_SIZE);

    uint8_t* slot_ctx = input + ctx_size;
    uint8_t* ep0_ctx = input + (ctx_size * 2u);
    xhci_ctx_write32(input, 4, 0x3);
    xhci_ctx_write32(slot_ctx, 0, 0);
    xhci_ctx_write32(slot_ctx, 4, (((portsc >> 10) & 0xFu) << 20) | (1u << 27));
    xhci_ctx_write32(slot_ctx, 8, port_id << 16);
    xhci_ctx_write64(ep0_ctx, 0, (uint64_t)ep0_ring | 1u);
    xhci_ctx_write32(ep0_ctx, 8, 0);
    xhci_ctx_write32(ep0_ctx, 12, (4u << 3) | (xhci_default_ep0_packet(portsc) << 16));

    dcbaa[slot_id] = (uint64_t)output;
    if (xhci_cmd(db, ir, (uint64_t)input, 0, (XHCI_TRB_TYPE_ADDRESS_DEVICE << 10) | XHCI_TRB_IOC | (slot_id << 24), &slot_id) != 0) {
        kprint("    xHCI: Address Device failed on port ");
        kprint_int(port_id);
        kprint("\n");
        return -1;
    }

    kprint("    xHCI: addressed device on port ");
    kprint_int(port_id);
    kprint(" as slot ");
    kprint_int(slot_id);
    kprint("\n");
    return 0;
}

static void xhci_init_rings(uint8_t* base, volatile uint32_t* op, volatile uint32_t* rt, uint32_t hcsparams1, uint32_t hcsparams2, uint32_t hccparams) {
    uint8_t max_slots = (uint8_t)(hcsparams1 & 0xFFu);
    uint32_t scratchpad_count = ((hcsparams2 >> 27) & 0x1Fu) | (((hcsparams2 >> 21) & 0x1Fu) << 5);

    uint32_t dcbaa_slots = max_slots + 1u;
    if (dcbaa_slots < XHCI_MAX_SLOTS)
        dcbaa_slots = XHCI_MAX_SLOTS;
    dcbaa = (uint64_t*)xhci_alloc_pages(xhci_page_count(dcbaa_slots * sizeof(uint64_t)));
    xhci_zero(dcbaa, dcbaa_slots * sizeof(uint64_t));

    if (scratchpad_count) {
        uint64_t* scratch = (uint64_t*)xhci_alloc_pages(xhci_page_count(scratchpad_count * sizeof(uint64_t)));
        xhci_zero(scratch, scratchpad_count * sizeof(uint64_t));
        for (uint32_t i = 0; i < scratchpad_count; i++)
            scratch[i] = (uint64_t)vm_alloc_page();
        dcbaa[0] = (uint64_t)scratch;
    }

    cmd_ring = (XhciTrb*)xhci_alloc_pages(xhci_page_count(sizeof(XhciTrb) * XHCI_CMD_RING_TRBS));
    event_ring = (XhciTrb*)xhci_alloc_pages(xhci_page_count(sizeof(XhciTrb) * XHCI_EVENT_RING_TRBS));
    XhciErstEntry* erst = (XhciErstEntry*)xhci_alloc_pages(1);
    xhci_zero(cmd_ring, sizeof(XhciTrb) * XHCI_CMD_RING_TRBS);
    xhci_zero(event_ring, sizeof(XhciTrb) * XHCI_EVENT_RING_TRBS);
    xhci_zero(erst, sizeof(XhciErstEntry));

    cmd_index = 0;
    cmd_cycle = 1;
    event_index = 0;
    event_cycle = 1;

    erst[0].ring_segment_base_lo = (uint32_t)(uint64_t)event_ring;
    erst[0].ring_segment_base_hi = (uint32_t)((uint64_t)event_ring >> 32);
    erst[0].ring_segment_size = XHCI_EVENT_RING_TRBS;

    uint32_t max = max_slots;
    if (max > XHCI_MAX_SLOTS - 1u)
        max = XHCI_MAX_SLOTS - 1u;
    mmio_write32(op + (0x38u / 4u), max);
    mmio_write64(op + (0x30u / 4u), (uint64_t)dcbaa);
    mmio_write64(op + (0x18u / 4u), (uint64_t)cmd_ring | 1u);

    uint32_t dboff = mmio_read32((volatile uint32_t*)(base + 0x14)) & ~3u;
    (void)dboff;
    (void)hccparams;

    volatile uint32_t* ir0 = rt + (0x20u / 4u);
    mmio_write32(ir0 + (0x08u / 4u), 1);
    mmio_write64(ir0 + (0x10u / 4u), (uint64_t)erst);
    mmio_write64(ir0 + (0x18u / 4u), (uint64_t)&event_ring[0]);
}

static void xhci_ports_report(volatile uint32_t* opregs, uint8_t n_ports) {
    kprint("  xHCI port status (after reset):\n");
    for (uint8_t i = 1; i <= n_ports; i++) {
        volatile uint32_t* portsc = opregs + (0x400u / 4u) + ((uint32_t)i - 1u) * (16u / 4u);
        uint32_t sc = mmio_read32(portsc);
        int ccs = (sc & 1u) != 0;
        kprint("    Port ");
        kprint_int(i);
        kprint(": ");
        kprint(ccs ? "device" : "empty");
        kprint(", link ");
        kprint(xhci_port_speed_name(sc));
        kprint("\n");
    }
}

int xhci_init(void) {
    PciAddress addr;
    if (pci_find_class(XHCI_PCI_CLASS_SERIAL, XHCI_PCI_SUBCLASS_USB, XHCI_PCI_PROGIF_XHCI, &addr) != 0) {
        kprint("xHCI: no PCI device found\n");
        return 0;
    }

    uint32_t cmdsts = pci_read32(addr, 0x04);
    pci_write32(addr, 0x04, cmdsts | (1u << 1) | (1u << 2));

    uint64_t bar_phys = pci_bar(addr, 0);
    if (bar_phys == 0) {
        kprint("xHCI: invalid MMIO BAR\n");
        return 0;
    }

    uint8_t* base = (uint8_t*)(uintptr_t)bar_phys;
    uint8_t caplen = base[0];
    volatile uint32_t* cap32 = (volatile uint32_t*)base;
    uint32_t hcsp1 = mmio_read32(cap32 + 1);
    uint32_t hcsp2 = mmio_read32(cap32 + 2);
    uint32_t hccparams = mmio_read32(cap32 + 4);
    uint32_t dboff = mmio_read32(cap32 + 5) & ~3u;
    uint32_t rtsoff = mmio_read32(cap32 + 6) & ~0x1Fu;

    uint8_t n_ports = (uint8_t)(hcsp1 & 0xFFu);

    kprint("xHCI: controller at ");
    kprint_hex((uintptr_t)bar_phys);
    kprint(", ");
    kprint_int(n_ports);
    kprint(" root port(s)\n");

    xhci_print_protocol_caps(base, hccparams);

    volatile uint32_t* op = (volatile uint32_t*)(base + caplen);
    volatile uint32_t* db = (volatile uint32_t*)(base + dboff);
    volatile uint32_t* rt = (volatile uint32_t*)(base + rtsoff);

    mmio_write32(op, mmio_read32(op) & ~USBCMD_RS);
    for (uint32_t spin = 0; spin < 1000000u; spin++) {
        if (mmio_read32(op + 1) & USBSTS_HCH)
            break;
    }

    mmio_write32(op, mmio_read32(op) | USBCMD_HCRST);
    for (uint32_t spin = 0; spin < 1000000u; spin++) {
        if ((mmio_read32(op) & USBCMD_HCRST) == 0)
            break;
    }
    for (uint32_t spin = 0; spin < 1000000u; spin++) {
        if ((mmio_read32(op + 1) & USBSTS_CNR) == 0)
            break;
    }

    xhci_init_rings(base, op, rt, hcsp1, hcsp2, hccparams);
    mmio_write32(op + 1, mmio_read32(op + 1));
    mmio_write32(op, mmio_read32(op) | USBCMD_RS | USBCMD_INTE);
    for (uint32_t spin = 0; spin < 1000000u; spin++) {
        if ((mmio_read32(op + 1) & USBSTS_HCH) == 0)
            break;
    }

    xhci_ports_report(op, n_ports);
    uint8_t context_size_64 = (hccparams & (1u << 2)) != 0;
    for (uint8_t i = 1; i <= n_ports; i++) {
        volatile uint32_t* portsc = op + (0x400u / 4u) + ((uint32_t)i - 1u) * (16u / 4u);
        uint32_t sc = mmio_read32(portsc);
        if (sc & 1u)
            xhci_address_port(db, rt + (0x20u / 4u), i, sc, context_size_64);
    }
    return 1;
}
