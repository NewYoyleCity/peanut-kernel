/* hda.c -- Intel High Definition Audio controller driver.
 *
 * Initialises the HDA controller: resets the hardware, allocates CORB
 * and RIRB DMA buffers, and starts the command/response engine.
 */

#include "hda.h"
#include "drivers/bus/pci.h"
#include "freelib/kstdio.h"
#include "freelib/kstdint.h"
#include "mm/vm.h"

#define HDA_GCAP     0x00
#define HDA_GCTL     0x08
#define HDA_STATESTS 0x0E
#define HDA_CORBLBASE 0x40
#define HDA_CORBUBASE 0x44
#define HDA_CORBWP    0x48
#define HDA_CORBRP    0x4A
#define HDA_CORBCTL   0x4C
#define HDA_CORBSIZE  0x4E
#define HDA_RIRBLBASE 0x50
#define HDA_RIRBUBASE 0x54
#define HDA_RIRBWP    0x58
#define HDA_RIRBCTL   0x5C
#define HDA_RIRBSIZE  0x5E

static volatile uint8_t* hda_mmio;


/* hda_read16 -- read 16-bit from HDA MMIO.
 */static uint16_t hda_read16(uint16_t reg) {
    return *(volatile uint16_t*)(hda_mmio + reg);
}


/* hda_read32 -- read 32-bit from HDA MMIO.
 */static uint32_t hda_read32(uint16_t reg) {
    return *(volatile uint32_t*)(hda_mmio + reg);
}


/* hda_write8 -- write 8-bit to HDA MMIO.
 */static void hda_write8(uint16_t reg, uint8_t v) {
    *(volatile uint8_t*)(hda_mmio + reg) = v;
}


/* hda_write16 -- write 16-bit to HDA MMIO.
 */static void hda_write16(uint16_t reg, uint16_t v) {
    *(volatile uint16_t*)(hda_mmio + reg) = v;
}


/* hda_write32 -- write 32-bit to HDA MMIO.
 */static void hda_write32(uint16_t reg, uint32_t v) {
    *(volatile uint32_t*)(hda_mmio + reg) = v;
}

void hda_init(void) {
    PciAddress addr;
    if (pci_find_class(0x04, 0x03, 0x00, &addr) != 0) {
        kprint("HDA: no PCI audio device found\n");
        return;
    }

    uint64_t bar = pci_bar(addr, 0);
    if (!bar || (bar & 1u)) {
        kprint("HDA: invalid MMIO BAR\n");
        return;
    }

    uint32_t cmdsts = pci_read32(addr, 0x04);
    pci_write32(addr, 0x04, cmdsts | (1u << 1) | (1u << 2));
    hda_mmio = (volatile uint8_t*)(uintptr_t)bar;

    hda_write32(HDA_GCTL, hda_read32(HDA_GCTL) & ~1u);
    for (uint32_t spin = 0; spin < 100000; spin++) {
        if ((hda_read32(HDA_GCTL) & 1u) == 0)
            break;
    }
    hda_write32(HDA_GCTL, hda_read32(HDA_GCTL) | 1u);
    for (uint32_t spin = 0; spin < 100000; spin++) {
        if (hda_read32(HDA_GCTL) & 1u)
            break;
    }

    uint32_t* corb = (uint32_t*)vm_alloc_page();
    uint64_t* rirb = (uint64_t*)vm_alloc_page();
    hda_write8(HDA_CORBCTL, 0);
    hda_write8(HDA_RIRBCTL, 0);
    hda_write32(HDA_CORBLBASE, (uint32_t)(uint64_t)corb);
    hda_write32(HDA_CORBUBASE, (uint32_t)((uint64_t)corb >> 32));
    hda_write32(HDA_RIRBLBASE, (uint32_t)(uint64_t)rirb);
    hda_write32(HDA_RIRBUBASE, (uint32_t)((uint64_t)rirb >> 32));
    hda_write8(HDA_CORBSIZE, 0x02);
    hda_write8(HDA_RIRBSIZE, 0x02);
    hda_write16(HDA_CORBRP, 0x8000);
    hda_write16(HDA_CORBWP, 0);
    hda_write16(HDA_RIRBWP, 0x8000);
    hda_write8(HDA_CORBCTL, 0x02);
    hda_write8(HDA_RIRBCTL, 0x02);

    kprint("HDA: controller initialized, codec state ");
    kprint_hex(hda_read16(HDA_STATESTS));
    kprint(", gcap ");
    kprint_hex(hda_read16(HDA_GCAP));
    kprint("\n");
}
