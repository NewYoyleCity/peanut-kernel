/* ac97.c -- Intel AC97 audio controller driver.
 *
 * Locates the AC97 controller via PCI, resets the bus-master engine,
 * and unmutes the master volume.  No PCM streaming is implemented.
 */

#include "ac97.h"
#include "drivers/bus/io.h"
#include "drivers/bus/pci.h"
#include "freelib/kstdio.h"
#include "freelib/kstdint.h"

#define AC97_VENDOR_INTEL 0x8086
#define AC97_DEVICE_ICH  0x2415
#define AC97_DEVICE_ICH2 0x2445
#define AC97_DEVICE_ICH4 0x24C5
#define AC97_DEVICE_QEMU 0x266E


/* ac97_find -- search PCI for an AC97-compatible audio controller.
 */static int ac97_find(PciAddress* out) {
    static const uint16_t ids[] = { AC97_DEVICE_ICH, AC97_DEVICE_ICH2, AC97_DEVICE_ICH4, AC97_DEVICE_QEMU };
    for (uint32_t i = 0; i < sizeof(ids) / sizeof(ids[0]); i++) {
        if (pci_find_device(AC97_VENDOR_INTEL, ids[i], out) == 0)
            return 0;
    }
    return pci_find_class(0x04, 0x01, 0x00, out);
}

void ac97_init(void) {
    PciAddress addr;
    if (ac97_find(&addr) != 0) {
        kprint("AC97: no PCI audio device found\n");
        return;
    }

    uint32_t cmdsts = pci_read32(addr, 0x04);
    pci_write32(addr, 0x04, cmdsts | (1u << 0) | (1u << 2));

    uint16_t nam = (uint16_t)(pci_bar(addr, 0) & ~3u);
    uint16_t nabm = (uint16_t)(pci_bar(addr, 1) & ~3u);
    if (!nam || !nabm) {
        kprint("AC97: invalid mixer or bus-master BAR\n");
        return;
    }

    outl(nabm + 0x2C, 0x00000002);
    for (uint32_t spin = 0; spin < 100000; spin++) {
        if ((inl(nabm + 0x2C) & 0x2u) == 0)
            break;
    }
    outw(nam + 0x02, 0x0000);
    outw(nam + 0x18, 0x0808);

    kprint("AC97: controller initialized\n");
}
