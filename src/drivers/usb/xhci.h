#ifndef XHCI_H
#define XHCI_H

#include "freelib/kstdint.h"

#ifdef __cplusplus
extern "C" {
#endif


#define XHCI_PCI_CLASS_SERIAL 0x0C
#define XHCI_PCI_SUBCLASS_USB 0x03
#define XHCI_PCI_PROGIF_XHCI 0x30

#define XHCI_MAX_DEVICES 32
#define XHCI_KBD_BUFFER 8

typedef struct {
    uint64_t parameter;
    uint32_t status;
    uint32_t control;
} __attribute__((packed)) XhciTrb;

typedef struct {
    uint32_t ring_segment_base_lo;
    uint32_t ring_segment_base_hi;
    uint32_t ring_segment_size;
    uint32_t reserved;
} __attribute__((packed)) XhciErstEntry;

typedef struct {
    uint32_t port_id;
    uint32_t slot_id;
    uint32_t speed;
    uint8_t  addressed;
    uint8_t  configured;
    uint8_t* input_ctx;
    uint8_t* output_ctx;
    XhciTrb* ep0_ring;
    XhciTrb* intr_ring;
    uint8_t  kbd_buf[XHCI_KBD_BUFFER];
} XhciDeviceInfo;

int xhci_init(void);
uint32_t xhci_device_count(void);
const XhciDeviceInfo* xhci_get_device(uint32_t index);
int xhci_usb_kbd_poll_report(uint8_t report[8]);

#ifdef __cplusplus
}
#endif

#endif
