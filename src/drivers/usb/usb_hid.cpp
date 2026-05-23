#include "drivers/usb/usb_hid.hpp"
#include "drivers/usb/xhci.h"
#include "freelib/kstdio.h"

static const char unshifted_map[] =
    "\x00\x00\x00\x00"
    "abcdefghijklmnopqrstuvwxyz"
    "1234567890\n\x1b\b\t -=[]\\"
    "\x00;'`,./"
    "\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00"
    "\x00";

static const char shifted_map[] =
    "\x00\x00\x00\x00"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "!@#$%^&*()\n\x1b\b\t _+{}|"
    "\x00:\"~<>?"
    "\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00"
    "\x00";

static UsbHidKeyboard kbd_instance;

UsbHidKeyboard* UsbHidKeyboard::instance() {
    return &kbd_instance;
}

void UsbHidKeyboard::init() {
    for (int i = 0; i < 8; i++) prev_report_[i] = 0;
    kprint("USB HID: keyboard driver ready\n");
}

static char map_key_usb(uint8_t code, uint8_t mod) {
    bool shift = (mod & 0x22u) != 0;
    if (code < 4 || code >= 75) return 0;
    return shift ? shifted_map[code - 4] : unshifted_map[code - 4];
}

int UsbHidKeyboard::poll_char(char* out) {
    uint8_t cur[8];
    if (xhci_usb_kbd_poll_report(cur) != 0) return -1;
    for (int j = 2; j < 8; j++) {
        if (cur[j] && cur[j] != prev_report_[j]) {
            char c = map_key_usb(cur[j], cur[0]);
            for (int k = 0; k < 8; k++) prev_report_[k] = cur[k];
            if (c) { *out = c; return 0; }
        }
    }
    for (int k = 0; k < 8; k++) prev_report_[k] = cur[k];
    return -1;
}

extern "C" int usb_kbd_init(void) {
    kbd_instance.init();
    return 0;
}

extern "C" int usb_kbd_poll_char(char* out) {
    return kbd_instance.poll_char(out);
}
