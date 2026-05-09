#include "virtio_net.h"
#include "drivers/bus/pci.h"
#include "freelib/kstdio.h"

void virtio_net_init(void) {
    PciAddress addr;
    if (pci_find_device(0x1AF4, 0x1000, &addr) == 0 ||
        pci_find_device(0x1AF4, 0x1041, &addr) == 0) {
        kprint("virtio-net: device detected; packet I/O unavailable in this build\n");
    } else {
        kprint("virtio-net: no PCI device found\n");
    }
}
