/* xhci.c -- xHCI (USB 3.0) host controller driver.
 *
 * Initialises an xHCI controller, enumerates root ports, assigns
 * device slots, and manages endpoint rings for control and interrupt
 * transfers.  Supports keyboard and mouse HID devices.
 */

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

#define PORTSC_CCS (1u << 0)
#define PORTSC_PED (1u << 1)
#define PORTSC_PR  (1u << 4)
#define PORTSC_PEC (1u << 17)
#define PORTSC_WPR (1u << 31)

#define XHCI_EXT_PROTOCOL 2u

typedef struct {
    uint64_t dequeue;
    uint32_t ep_info;
    uint32_t ep_info2;
    uint32_t reserved[4];
} __attribute__((packed)) XhciEndpointContext;

typedef struct {
    uint32_t route_string;
    uint32_t speed_entries;
    uint32_t tt_info;
    uint32_t state;
    uint32_t reserved[4];
} __attribute__((packed)) XhciSlotContext;



XhciTrb* xhci_cmd_ring;
static uint32_t cmd_index;
static uint32_t cmd_cycle;
XhciTrb* xhci_event_ring;
static uint32_t event_index;
static uint32_t event_cycle;
static uint64_t* dcbaa;

XhciDeviceInfo xhci_devices[XHCI_MAX_DEVICES];
uint32_t xhci_device_count_;

volatile uint32_t* xhci_db_global;
volatile uint32_t* xhci_ir_global;


/* mmio_read32 -- read a 32-bit MMIO register.
 */static uint32_t mmio_read32(volatile uint32_t* r) {
    return *r;
}


/* mmio_write32 -- write a 32-bit MMIO register.
 */static void mmio_write32(volatile uint32_t* r, uint32_t v) {
    *r = v;
}


/* mmio_write64 -- write a 64-bit MMIO register as two 32-bit writes.
 */static void mmio_write64(volatile uint32_t* r, uint64_t v) {
    r[0] = (uint32_t)v;
    r[1] = (uint32_t)(v >> 32);
}


/* trb_type -- extract the TRB type from a control dword.
 */static uint32_t trb_type(uint32_t control) {
    return (control >> 10) & 0x3Fu;
}

void* xhci_alloc_pages(uint32_t pages) {
    void* first = NULL;
    for (uint32_t i = 0; i < pages; i++) {
        void* p = vm_alloc_page();
        if (!first)
            first = p;
    }
    return first;
}


/* xhci_page_count -- compute number of 4 KB pages needed for a byte count.
 */static uint32_t xhci_page_count(uint32_t bytes) {
    return (bytes + VM_PAGE_SIZE - 1u) / VM_PAGE_SIZE;
}

void xhci_zero(void* ptr, uint32_t bytes) {
    uint8_t* p = (uint8_t*)ptr;
    for (uint32_t i = 0; i < bytes; i++)
        p[i] = 0;
}


/* xhci_ctx_write32 -- write 32-bit value into an xHCI context.
 */static void xhci_ctx_write32(uint8_t* ctx, uint32_t offset, uint32_t value) {
    *(uint32_t*)(ctx + offset) = value;
}


/* xhci_ctx_write64 -- write 64-bit value into an xHCI context.
 */static void xhci_ctx_write64(uint8_t* ctx, uint32_t offset, uint64_t value) {
    *(uint64_t*)(ctx + offset) = value;
}


/* xhci_port_speed_name -- return string name for a USB port speed.
 */static const char* xhci_port_speed_name(uint32_t portsc) {
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


/* xhci_print_protocol_caps -- print USB protocol capability entries.
 */static void xhci_print_protocol_caps(uint8_t* base, uint32_t hccparams) {
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


/* xhci_wait_cmd -- wait for a command completion event on the event ring.
 */static int xhci_wait_cmd(volatile uint32_t* db, volatile uint32_t* ir, uint64_t expected_trb, uint32_t* slot_id) {
    *slot_id = 0;
    db[0] = 0;
    for (uint32_t spin = 0; spin < 4000000u; spin++) {
        XhciTrb* ev = &xhci_event_ring[event_index];
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
            mmio_write64(ir + (0x18u / 4u), (uint64_t)&xhci_event_ring[event_index]);
            return command == (expected_trb & ~0xFu) && code == 1u ? 0 : -1;
        }
    }
    return -1;
}


/* xhci_cmd -- enqueue a command TRB and wait for its completion.
 */static int xhci_cmd(volatile uint32_t* db, volatile uint32_t* ir, uint64_t parameter, uint32_t status, uint32_t control, uint32_t* slot_id) {
    XhciTrb* trb = &xhci_cmd_ring[cmd_index];
    trb->parameter = parameter;
    trb->status = status;
    trb->control = control | (cmd_cycle ? XHCI_TRB_CYCLE : 0u);
    uint64_t expected = (uint64_t)trb;

    cmd_index++;
    if (cmd_index == XHCI_CMD_RING_TRBS - 1u) {
        xhci_cmd_ring[cmd_index].parameter = (uint64_t)&xhci_cmd_ring[0];
        xhci_cmd_ring[cmd_index].status = 0;
        xhci_cmd_ring[cmd_index].control = (XHCI_TRB_TYPE_LINK << 10) | (1u << 1) | (cmd_cycle ? XHCI_TRB_CYCLE : 0u);
        cmd_index = 0;
        cmd_cycle ^= 1u;
    }

    return xhci_wait_cmd(db, ir, expected, slot_id);
}

int xhci_wait_xfer_events(volatile uint32_t* ir, uint32_t count) {
    for (uint32_t spin = 0; spin < 4000000u; spin++) {
        XhciTrb* ev = &xhci_event_ring[event_index];
        if ((ev->control & XHCI_TRB_CYCLE) == event_cycle &&
            trb_type(ev->control) == XHCI_TRB_TYPE_TRANSFER_EVENT) {
            uint32_t code = (ev->status >> 24) & 0xFFu;
            event_index++;
            if (event_index == XHCI_EVENT_RING_TRBS) {
                event_index = 0;
                event_cycle ^= 1u;
            }
            mmio_write64(ir + (0x18u / 4u), (uint64_t)&xhci_event_ring[event_index]);
            if (code != 1u) return -1;
            count--;
            if (count == 0) return 0;
        }
    }
    return -1;
}

XhciTrb* xhci_find_ep0_ring(uint32_t slot_id) {
    for (uint32_t i = 0; i < xhci_device_count_; i++) {
        if (xhci_devices[i].slot_id == slot_id)
            return xhci_devices[i].ep0_ring;
    }
    return NULL;
}

XhciTrb* xhci_find_intr_ring(uint32_t slot_id) {
    for (uint32_t i = 0; i < xhci_device_count_; i++) {
        if (xhci_devices[i].slot_id == slot_id)
            return xhci_devices[i].intr_ring;
    }
    return NULL;
}

int xhci_do_control(volatile uint32_t* db, volatile uint32_t* ir, uint32_t slot_id, const uint8_t* setup, uint32_t setup_words, uint32_t trt, uint32_t* xfer_ring_cycle) {
    XhciTrb* ring = xhci_find_ep0_ring(slot_id);
    if (!ring) return -1;
    XhciTrb* trb = &ring[0];
    uint64_t setup_pkt = 0;
    for (uint32_t i = 0; i < setup_words && i < 2; i++)
        setup_pkt |= (uint64_t)((uint32_t*)setup)[i] << (i * 32u);
    trb->parameter = setup_pkt;
    trb->status = 8;
    trb->control = (XHCI_TRB_TYPE_SETUP_STAGE << 10) | XHCI_TRB_IOC | XHCI_TRB_IDT |
                   (trt << 5) | (*xfer_ring_cycle ? XHCI_TRB_CYCLE : 0u);
    trb = &ring[1];
    trb->parameter = 0;
    trb->status = 0;
    trb->control = (XHCI_TRB_TYPE_STATUS_STAGE << 10) | XHCI_TRB_IOC |
                   ((trt == 2u) ? XHCI_TRB_DIR_IN : 0u) |
                   (*xfer_ring_cycle ? XHCI_TRB_CYCLE : 0u);
    db[slot_id] = 1;
    int ret = xhci_wait_xfer_events(ir, 2);
    *xfer_ring_cycle ^= 1u;
    return ret;
}

int xhci_queue_intr(volatile uint32_t* db, volatile uint32_t* ir, uint32_t slot_id, uint8_t* buf, uint32_t len, uint32_t* xfer_ring_cycle) {
    XhciTrb* ring = xhci_find_intr_ring(slot_id);
    if (!ring) return -1;
    XhciTrb* trb = &ring[0];
    trb->parameter = (uint64_t)buf;
    trb->status = len;
    trb->control = (XHCI_TRB_TYPE_NORMAL << 10) | XHCI_TRB_IOC |
                   (*xfer_ring_cycle ? XHCI_TRB_CYCLE : 0u);
    db[slot_id] = 3;
    int ret = xhci_wait_xfer_events(ir, 1);
    *xfer_ring_cycle ^= 1u;
    return ret;
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

static int xhci_address_port(volatile uint32_t* db, volatile uint32_t* ir, uint32_t port_id, uint32_t portsc, uint8_t context_size_64, uint32_t dev_idx) {
    uint32_t slot_id = 0;
    if (xhci_cmd(db, ir, 0, 0, XHCI_TRB_TYPE_ENABLE_SLOT << 10, &slot_id) != 0 || slot_id == 0 || slot_id >= XHCI_MAX_SLOTS) {
        kprint("    xHCI: Enable Slot failed\n");
        return -1;
    }

    uint32_t ctx_size = context_size_64 ? 64u : 32u;
    uint32_t input_bytes = ctx_size * 3u;
    uint8_t* input = (uint8_t*)xhci_alloc_pages(xhci_page_count(input_bytes));
    void* output = xhci_alloc_pages(xhci_page_count(ctx_size * 32u));
    XhciTrb* ep0_ring = (XhciTrb*)xhci_alloc_pages(2);
    XhciTrb* intr_ring = (XhciTrb*)xhci_alloc_pages(1);
    if (!input || !output || !ep0_ring || !intr_ring)
        return -1;

    xhci_zero(input, input_bytes);
    xhci_zero(output, ctx_size * 32u);
    xhci_zero(ep0_ring, 2u * VM_PAGE_SIZE);
    xhci_zero(intr_ring, VM_PAGE_SIZE);

    uint8_t* slot_ctx = input + ctx_size;
    uint8_t* ep0_ctx = input + (ctx_size * 2u);
    xhci_ctx_write32(input, 4, 0x3);
    xhci_ctx_write32(slot_ctx, 0, 0);
    xhci_ctx_write32(slot_ctx, 4, (((portsc >> 10) & 0xFu) << 20) | (1u << 27));
    xhci_ctx_write32(slot_ctx, 8, port_id << 16);
    xhci_ctx_write64(ep0_ctx, 0, (uint64_t)ep0_ring | 1u);
    xhci_ctx_write32(ep0_ctx, 8, 0);
    xhci_ctx_write32(ep0_ctx, 12, (XHCI_EP_TYPE_CONTROL << 3) | (xhci_default_ep0_packet(portsc) << 16));

    dcbaa[slot_id] = (uint64_t)output;
    if (xhci_cmd(db, ir, (uint64_t)input, 0, (XHCI_TRB_TYPE_ADDRESS_DEVICE << 10) | XHCI_TRB_IOC | (slot_id << 24), &slot_id) != 0) {
        kprint("    xHCI: Address Device failed on port ");
        kprint_int(port_id);
        kprint("\n");
        return -1;
    }

    XhciDeviceInfo* info = &xhci_devices[dev_idx];
    info->slot_id = slot_id;
    info->input_ctx = input;
    info->output_ctx = (uint8_t*)output;
    info->ep0_ring = ep0_ring;
    info->intr_ring = intr_ring;
    info->device_kind = XHCI_DEV_UNKNOWN;
    for (uint32_t i = 0; i < XHCI_KBD_BUFFER; i++)
        info->kbd_buf[i] = 0;
    for (uint32_t i = 0; i < XHCI_MOUSE_BUFFER; i++)
        info->mouse_buf[i] = 0;
    info->kbd_xfer_cycle = 0;
    info->mouse_xfer_cycle = 0;

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

    xhci_cmd_ring = (XhciTrb*)xhci_alloc_pages(xhci_page_count(sizeof(XhciTrb) * XHCI_CMD_RING_TRBS));
    xhci_event_ring = (XhciTrb*)xhci_alloc_pages(xhci_page_count(sizeof(XhciTrb) * XHCI_EVENT_RING_TRBS));
    XhciErstEntry* erst = (XhciErstEntry*)xhci_alloc_pages(1);
    xhci_zero(xhci_cmd_ring, sizeof(XhciTrb) * XHCI_CMD_RING_TRBS);
    xhci_zero(xhci_event_ring, sizeof(XhciTrb) * XHCI_EVENT_RING_TRBS);
    xhci_zero(erst, sizeof(XhciErstEntry));

    cmd_index = 0;
    cmd_cycle = 1;
    event_index = 0;
    event_cycle = 1;

    erst[0].ring_segment_base_lo = (uint32_t)(uint64_t)xhci_event_ring;
    erst[0].ring_segment_base_hi = (uint32_t)((uint64_t)xhci_event_ring >> 32);
    erst[0].ring_segment_size = XHCI_EVENT_RING_TRBS;

    uint32_t max = max_slots;
    if (max > XHCI_MAX_SLOTS - 1u)
        max = XHCI_MAX_SLOTS - 1u;
    mmio_write32(op + (0x38u / 4u), max);
    mmio_write64(op + (0x30u / 4u), (uint64_t)dcbaa);
    mmio_write64(op + (0x18u / 4u), (uint64_t)xhci_cmd_ring | 1u);

    uint32_t dboff = mmio_read32((volatile uint32_t*)(base + 0x14)) & ~3u;
    (void)dboff;
    (void)hccparams;

    volatile uint32_t* ir0 = rt + (0x20u / 4u);
    mmio_write32(ir0 + (0x08u / 4u), 1);
    mmio_write64(ir0 + (0x10u / 4u), (uint64_t)erst);
    mmio_write64(ir0 + (0x18u / 4u), (uint64_t)&xhci_event_ring[0]);
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



static void xhci_reset_port(volatile uint32_t* portsc, uint32_t port_id) {
    kprint("    Resetting port ");
    kprint_int(port_id);
    kprint("...\n");
    mmio_write32(portsc, mmio_read32(portsc) | PORTSC_PR);
    for (uint32_t spin = 0; spin < 10000000u; spin++) {
        if ((mmio_read32(portsc) & PORTSC_PR) == 0)
            break;
    }
    for (uint32_t spin = 0; spin < 1000000u; spin++) {
        if ((mmio_read32(portsc) & PORTSC_PED))
            break;
    }
    kprint("      done\n");
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

    xhci_db_global = db;
    xhci_ir_global = rt + (0x20u / 4u);

    xhci_ports_report(op, n_ports);
    uint8_t context_size_64 = (hccparams & (1u << 2)) != 0;
    xhci_device_count_ = 0;
    for (uint8_t i = 1; i <= n_ports && xhci_device_count_ < XHCI_MAX_DEVICES; i++) {
        volatile uint32_t* portsc = op + (0x400u / 4u) + ((uint32_t)i - 1u) * (16u / 4u);
        uint32_t sc = mmio_read32(portsc);
        if (sc & PORTSC_CCS) {
            xhci_reset_port(portsc, i);
            sc = mmio_read32(portsc);
            uint32_t dev_idx = xhci_device_count_;
            xhci_devices[dev_idx].port_id = i;
            xhci_devices[dev_idx].slot_id = 0;
            xhci_devices[dev_idx].speed = (sc >> 10) & 0xFu;
            xhci_devices[dev_idx].addressed = 1;
            xhci_devices[dev_idx].configured = 0;
            if (xhci_address_port(db, rt + (0x20u / 4u), i, sc, context_size_64, dev_idx) == 0) {
                xhci_devices[dev_idx].addressed = 1;
                xhci_hid_configure(db, rt + (0x20u / 4u), dev_idx);
                xhci_device_count_++;
            }
        }
    }
    return 1;
}

uint32_t xhci_device_count(void) {
    return xhci_device_count_;
}

const XhciDeviceInfo* xhci_get_device(uint32_t index) {
    if (index >= xhci_device_count_)
        return NULL;
    return &xhci_devices[index];
}


