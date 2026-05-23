#ifndef USB_HID_HPP
#define USB_HID_HPP

#include "freelib/kstdint.h"

class UsbHidKeyboard {
public:
    void init();
    int poll_char(char* out);
    static UsbHidKeyboard* instance();

private:
    uint8_t prev_report_[8];
};

extern "C" int usb_kbd_poll_char(char* out);

#endif
