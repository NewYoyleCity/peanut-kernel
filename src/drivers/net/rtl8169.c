#include "rtl8169.h"
#include "drivers/bus/pci.h"
#include "freelib/kstdio.h"

void rtl8169_init(void) {
    PciAddress addr;
    if (pci_find_device(0x10EC, 0x8168, &addr) == 0 ||
        pci_find_device(0x10EC, 0x8169, &addr) == 0 ||
        pci_find_device(0x10EC, 0x8125, &addr) == 0) {
        kprint("RTL8169/RTL8168/RTL8125: device detected; packet I/O unavailable in this build\n");
    } else {
        kprint("RTL8169: no PCI device found\n");
    }
}
