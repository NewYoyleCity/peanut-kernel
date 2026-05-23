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
#define XHCI_MOUSE_BUFFER 4

/* USB standard requests */
#define USB_REQ_GET_DESCRIPTOR   0x06
#define USB_REQ_SET_CONFIGURATION 0x09
#define USB_REQ_SET_PROTOCOL     0x0B
#define USB_REQ_SET_IDLE         0x0A

#define USB_HID_PROTOCOL_BOOT 0

/* Descriptor types */
#define USB_DESC_DEVICE     1
#define USB_DESC_CONFIG     2
#define USB_DESC_INTERFACE  4
#define USB_DESC_HID        0x21

/* HID class codes from interface descriptor */
#define USB_HID_CLASS       3
#define USB_HID_SUBCLASS_BOOT   1
#define USB_HID_PROTOCOL_KBD   1
#define USB_HID_PROTOCOL_MOUSE 2

/* TRB types */
#define XHCI_TRB_TYPE_NORMAL          1u
#define XHCI_TRB_TYPE_SETUP_STAGE     2u
#define XHCI_TRB_TYPE_DATA_STAGE      3u
#define XHCI_TRB_TYPE_STATUS_STAGE    4u
#define XHCI_TRB_TYPE_LINK            6u
#define XHCI_TRB_TYPE_ENABLE_SLOT     9u
#define XHCI_TRB_TYPE_ADDRESS_DEVICE 11u
#define XHCI_TRB_TYPE_CONFIGURE_ENDPOINT 12u
#define XHCI_TRB_TYPE_TRANSFER_EVENT 32u
#define XHCI_TRB_TYPE_CMD_COMPLETION 33u

#define XHCI_TRB_CYCLE 1u
#define XHCI_TRB_IOC   (1u << 5)
#define XHCI_TRB_IDT   (1u << 16)
#define XHCI_TRB_CH    (1u << 4)
#define XHCI_TRB_DIR_IN (1u << 6)

/* EP types */
#define XHCI_EP_TYPE_CONTROL      4u
#define XHCI_EP_TYPE_INTERRUPT_IN 7u

#define XHCI_CMD_RING_TRBS   256u
#define XHCI_EVENT_RING_TRBS 256u

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

typedef enum {
    XHCI_DEV_NONE,
    XHCI_DEV_KEYBOARD,
    XHCI_DEV_MOUSE,
    XHCI_DEV_UNKNOWN
} XhciDeviceKind;

typedef struct {
    uint32_t port_id;
    uint32_t slot_id;
    uint32_t speed;
    uint8_t  addressed;
    uint8_t  configured;
    uint8_t  device_kind;
    uint8_t* input_ctx;
    uint8_t* output_ctx;
    XhciTrb* ep0_ring;
    XhciTrb* intr_ring;
    uint8_t  kbd_buf[XHCI_KBD_BUFFER];
    uint8_t  mouse_buf[XHCI_MOUSE_BUFFER];
    uint32_t kbd_xfer_cycle;
    uint32_t mouse_xfer_cycle;
} XhciDeviceInfo;

/* Shared globals (defined in xhci.c, used by xhci_hid.c) */
#define XHCI_MAX_SLOTS 64u
extern XhciDeviceInfo xhci_devices[XHCI_MAX_DEVICES];
extern uint32_t xhci_device_count_;
extern volatile uint32_t* xhci_db_global;
extern volatile uint32_t* xhci_ir_global;

/* Core xHCI functions */
int xhci_init(void);
uint32_t xhci_device_count(void);
const XhciDeviceInfo* xhci_get_device(uint32_t index);

/* Shared internal helpers (needed by xhci_hid.c) */
XhciTrb* xhci_find_ep0_ring(uint32_t slot_id);
XhciTrb* xhci_find_intr_ring(uint32_t slot_id);
int xhci_do_control(volatile uint32_t* db, volatile uint32_t* ir,
                    uint32_t slot_id, const uint8_t* setup,
                    uint32_t setup_words, uint32_t trt,
                    uint32_t* xfer_ring_cycle);
int xhci_queue_intr(volatile uint32_t* db, volatile uint32_t* ir,
                    uint32_t slot_id, uint8_t* buf, uint32_t len,
                    uint32_t* xfer_ring_cycle);
void* xhci_alloc_pages(uint32_t pages);
void xhci_zero(void* ptr, uint32_t bytes);
int xhci_wait_xfer_events(volatile uint32_t* ir, uint32_t count);

/* HID (keyboard + mouse) */
int xhci_usb_kbd_poll_report(uint8_t report[8]);
int xhci_usb_mouse_poll_report(uint8_t report[4]);
void xhci_hid_configure(volatile uint32_t* db, volatile uint32_t* ir, uint32_t dev_idx);

#ifdef __cplusplus
}
#endif

#endif
