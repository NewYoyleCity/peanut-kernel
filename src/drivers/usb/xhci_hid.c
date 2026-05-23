/* xhci_hid.c -- xHCI HID (keyboard/mouse) device configuration.
 *
 * Configures USB HID devices attached to xHCI ports: reads device
 * and configuration descriptors, determines if the device is a
 * keyboard or mouse, and starts interrupt transfers.
 */

#include "drivers/usb/xhci.h"
#include "freelib/kstdio.h"
#include "freelib/kstdint.h"
#include "mm/vm.h"

static uint8_t xhci_get_descriptor(volatile uint32_t* db, volatile uint32_t* ir,
                                    uint32_t slot_id, uint8_t desc_type,
                                    uint8_t desc_index, uint8_t* buf,
                                    uint32_t buf_len, uint32_t* xfer_cycle) {
    uint8_t setup[8];
    for (uint32_t i = 0; i < 8; i++) setup[i] = 0;
    setup[0] = 0x80;
    setup[1] = USB_REQ_GET_DESCRIPTOR;
    setup[2] = desc_type;
    setup[3] = desc_index;
    setup[5] = (uint8_t)buf_len;

    XhciTrb* ring = xhci_find_ep0_ring(slot_id);
    if (!ring) return 0;

    /* SETUP stage — TRT=3 for IN data */
    uint64_t setup_pkt = 0;
    for (uint32_t i = 0; i < 2; i++)
        setup_pkt |= (uint64_t)((uint32_t*)setup)[i] << (i * 32u);
    XhciTrb* trb = &ring[0];
    trb->parameter = setup_pkt;
    trb->status = 8;
    trb->control = (XHCI_TRB_TYPE_SETUP_STAGE << 10) | XHCI_TRB_IOC | XHCI_TRB_IDT |
                   (3u << 5) | (*xfer_cycle ? XHCI_TRB_CYCLE : 0u);

    /* DATA stage */
    trb = &ring[1];
    trb->parameter = (uint64_t)buf;
    trb->status = buf_len;
    trb->control = (XHCI_TRB_TYPE_DATA_STAGE << 10) | XHCI_TRB_IOC | XHCI_TRB_DIR_IN | XHCI_TRB_CH |
                   (*xfer_cycle ? XHCI_TRB_CYCLE : 0u);

    /* STATUS stage */
    trb = &ring[2];
    trb->parameter = 0;
    trb->status = 0;
    trb->control = (XHCI_TRB_TYPE_STATUS_STAGE << 10) | XHCI_TRB_IOC |
                   (*xfer_cycle ? XHCI_TRB_CYCLE : 0u);

    db[slot_id] = 1;
    int ret = xhci_wait_xfer_events(ir, 3);
    *xfer_cycle ^= 1u;
    return ret == 0 ? 1 : 0;
}

static int xhci_device_is_mouse(volatile uint32_t* db, volatile uint32_t* ir,
                                 uint32_t slot_id, uint32_t* xfer_cycle) {
    uint8_t buf[18];
    if (!xhci_get_descriptor(db, ir, slot_id, USB_DESC_DEVICE, 0, buf, 18, xfer_cycle))
        return 0;
    uint8_t dev_class = buf[4];
    uint8_t dev_subclass = buf[5];
    uint8_t dev_protocol = buf[6];
    if (dev_class == 3 && dev_subclass == 1 && dev_protocol == 2)
        return 1;
    if (dev_class != 0)
        return 0;

    uint8_t cfg_buf[9];
    if (!xhci_get_descriptor(db, ir, slot_id, USB_DESC_CONFIG, 0, cfg_buf, 9, xfer_cycle))
        return 0;
    uint16_t cfg_total = (uint16_t)cfg_buf[2] | ((uint16_t)cfg_buf[3] << 8);
    uint8_t cfg_full[256];
    uint32_t read_len = cfg_total > 256 ? 256 : cfg_total;
    if (!xhci_get_descriptor(db, ir, slot_id, USB_DESC_CONFIG, 0, cfg_full, read_len, xfer_cycle))
        return 0;

    uint32_t pos = 0;
    while (pos + 2 < read_len) {
        uint8_t dlen = cfg_full[pos];
        uint8_t dtype = cfg_full[pos + 1];
        if (dlen < 2) break;
        if (dtype == USB_DESC_INTERFACE && pos + 8 < read_len) {
            uint8_t iclass = cfg_full[pos + 5];
            uint8_t isub = cfg_full[pos + 6];
            uint8_t iproto = cfg_full[pos + 7];
            if (iclass == 3 && isub == 1 && (iproto == 1 || iproto == 2))
                return iproto == 2;
        }
        pos += dlen;
    }
    return 0;
}

void xhci_hid_configure(volatile uint32_t* db, volatile uint32_t* ir, uint32_t dev_idx) {
    XhciDeviceInfo* dev = &xhci_devices[dev_idx];
    if (!dev->addressed) return;

    uint8_t setup[8];
    uint32_t xfer_cycle = 1;

    int is_mouse = xhci_device_is_mouse(db, ir, dev->slot_id, &xfer_cycle);

    for (uint32_t i = 0; i < 8; i++) setup[i] = 0;
    setup[0] = 0x00; setup[1] = USB_REQ_SET_CONFIGURATION; setup[2] = 1;
    if (xhci_do_control(db, ir, dev->slot_id, setup, 2, 0, &xfer_cycle) != 0) {
        kprint("      xHCI: Set Configuration failed\n");
        return;
    }

    for (uint32_t i = 0; i < 8; i++) setup[i] = 0;
    setup[0] = 0x21; setup[1] = USB_REQ_SET_PROTOCOL;
    if (xhci_do_control(db, ir, dev->slot_id, setup, 2, 0, &xfer_cycle) != 0) {
        kprint("      xHCI: Set Protocol failed\n");
        return;
    }

    for (uint32_t i = 0; i < 8; i++) setup[i] = 0;
    setup[0] = 0x21; setup[1] = USB_REQ_SET_IDLE;
    if (xhci_do_control(db, ir, dev->slot_id, setup, 2, 0, &xfer_cycle) != 0) {
        return;
    }

    if (is_mouse) {
        dev->device_kind = XHCI_DEV_MOUSE;
        if (xhci_queue_intr(db, ir, dev->slot_id, dev->mouse_buf, XHCI_MOUSE_BUFFER, &dev->mouse_xfer_cycle) != 0) {
            kprint("      xHCI: mouse interrupt queue failed\n");
            return;
        }
        dev->configured = 1;
        kprint("      xHCI: USB mouse ready on slot ");
        kprint_int(dev->slot_id);
        kprint("\n");
    } else {
        dev->device_kind = XHCI_DEV_KEYBOARD;
        if (xhci_queue_intr(db, ir, dev->slot_id, dev->kbd_buf, XHCI_KBD_BUFFER, &dev->kbd_xfer_cycle) != 0) {
            kprint("      xHCI: keyboard interrupt queue failed\n");
            return;
        }
        dev->configured = 1;
        kprint("      xHCI: USB keyboard ready on slot ");
        kprint_int(dev->slot_id);
        kprint("\n");
    }
}

int xhci_usb_kbd_poll_report(uint8_t report[8]) {
    if (!xhci_db_global || !xhci_ir_global) return -1;
    for (uint32_t i = 0; i < xhci_device_count_; i++) {
        XhciDeviceInfo* dev = &xhci_devices[i];
        if (!dev->configured || dev->device_kind != XHCI_DEV_KEYBOARD) continue;
        if (xhci_queue_intr(xhci_db_global, xhci_ir_global, dev->slot_id, dev->kbd_buf, XHCI_KBD_BUFFER, &dev->kbd_xfer_cycle) != 0)
            return -1;
        for (uint32_t j = 0; j < 8; j++)
            report[j] = dev->kbd_buf[j];
        return 0;
    }
    return -1;
}

int xhci_usb_mouse_poll_report(uint8_t report[4]) {
    if (!xhci_db_global || !xhci_ir_global) return -1;
    for (uint32_t i = 0; i < xhci_device_count_; i++) {
        XhciDeviceInfo* dev = &xhci_devices[i];
        if (!dev->configured || dev->device_kind != XHCI_DEV_MOUSE) continue;
        if (xhci_queue_intr(xhci_db_global, xhci_ir_global, dev->slot_id, dev->mouse_buf, XHCI_MOUSE_BUFFER, &dev->mouse_xfer_cycle) != 0)
            return -1;
        for (uint32_t j = 0; j < 4; j++)
            report[j] = dev->mouse_buf[j];
        return 0;
    }
    return -1;
}

int usb_mouse_poll_packet(uint8_t packet[4]) {
    return xhci_usb_mouse_poll_report(packet);
}
