/* pci.c -- PCI configuration space access.
 *
 * Provides low-level read/write to PCI config space via I/O ports
 * 0xCF8/0xCFC, and higher-level helpers to find devices by class,
 * vendor/device ID, and to read BAR registers.
 */

#include "drivers/bus/pci.h"
#include "drivers/bus/io.h"

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC


/* pci_address -- construct the CONFIG_ADDRESS port value.
 */static uint32_t pci_address(PciAddress addr, uint8_t offset) {
    return 0x80000000u |
        ((uint32_t)addr.bus << 16) |
        ((uint32_t)addr.slot << 11) |
        ((uint32_t)addr.function << 8) |
        (offset & 0xFC);
}

uint32_t pci_read32(PciAddress addr, uint8_t offset) {
    outl(PCI_CONFIG_ADDRESS, pci_address(addr, offset));
    return inl(PCI_CONFIG_DATA);
}

void pci_write32(PciAddress addr, uint8_t offset, uint32_t value) {
    outl(PCI_CONFIG_ADDRESS, pci_address(addr, offset));
    outl(PCI_CONFIG_DATA, value);
}

int pci_find_class(uint8_t class_code, uint8_t subclass, uint8_t prog_if, PciAddress* out) {
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            for (uint8_t function = 0; function < 8; function++) {
                PciAddress addr = { (uint8_t)bus, slot, function };
                uint32_t vendor_device = pci_read32(addr, 0x00);
                if ((vendor_device & 0xFFFF) == 0xFFFF) {
                    if (function == 0) break;
                    continue;
                }

                uint32_t class_reg = pci_read32(addr, 0x08);
                uint8_t found_prog_if = (uint8_t)(class_reg >> 8);
                uint8_t found_subclass = (uint8_t)(class_reg >> 16);
                uint8_t found_class = (uint8_t)(class_reg >> 24);

                if (found_class == class_code &&
                    found_subclass == subclass &&
                    found_prog_if == prog_if) {
                    *out = addr;
                    return 0;
                }

                if (function == 0) {
                    uint32_t header = pci_read32(addr, 0x0C);
                    if (((header >> 16) & 0x80) == 0) break;
                }
            }
        }
    }

    return -1;
}

int pci_find_class_any_prog_if(uint8_t class_code, uint8_t subclass, PciAddress* out) {
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            for (uint8_t function = 0; function < 8; function++) {
                PciAddress addr = { (uint8_t)bus, slot, function };
                uint32_t vendor_device = pci_read32(addr, 0x00);
                if ((vendor_device & 0xFFFF) == 0xFFFF) {
                    if (function == 0) break;
                    continue;
                }

                uint32_t class_reg = pci_read32(addr, 0x08);
                uint8_t found_subclass = (uint8_t)(class_reg >> 16);
                uint8_t found_class = (uint8_t)(class_reg >> 24);

                if (found_class == class_code && found_subclass == subclass) {
                    *out = addr;
                    return 0;
                }

                if (function == 0) {
                    uint32_t header = pci_read32(addr, 0x0C);
                    if (((header >> 16) & 0x80) == 0) break;
                }
            }
        }
    }

    return -1;
}

int pci_find_device(uint16_t vendor_id, uint16_t device_id, PciAddress* out) {
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            for (uint8_t function = 0; function < 8; function++) {
                PciAddress addr = { (uint8_t)bus, slot, function };
                uint32_t vendor_device = pci_read32(addr, 0x00);
                if ((vendor_device & 0xFFFF) == 0xFFFF) {
                    if (function == 0) break;
                    continue;
                }

                if ((uint16_t)(vendor_device & 0xFFFFu) == vendor_id &&
                    (uint16_t)(vendor_device >> 16) == device_id) {
                    *out = addr;
                    return 0;
                }

                if (function == 0) {
                    uint32_t header = pci_read32(addr, 0x0C);
                    if (((header >> 16) & 0x80) == 0) break;
                }
            }
        }
    }

    return -1;
}

uint64_t pci_bar(PciAddress addr, uint8_t bar_index) {
    if (bar_index >= 6)
        return 0;

    uint8_t off = (uint8_t)(0x10 + bar_index * 4);
    uint32_t lo = pci_read32(addr, off);
    if (lo & 1u)
        return lo & ~3u;

    uint64_t bar = lo & ~0xFull;
    if ((lo & 0x6u) == 0x4u && bar_index < 5) {
        uint32_t hi = pci_read32(addr, off + 4);
        bar |= (uint64_t)hi << 32;
    }
    return bar;
}
