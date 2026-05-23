/* pci.h -- PCI configuration access types and prototypes.
 *
 * Defines the PciAddress structure and declares the public API
 * for reading/writing PCI config space and finding devices.
 */

#ifndef PCI_H
#define PCI_H

#include "freelib/kstdint.h"

typedef struct {
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
} PciAddress;

uint32_t pci_read32(PciAddress addr, uint8_t offset);
void pci_write32(PciAddress addr, uint8_t offset, uint32_t value);
int pci_find_class(uint8_t class_code, uint8_t subclass, uint8_t prog_if, PciAddress* out);
int pci_find_class_any_prog_if(uint8_t class_code, uint8_t subclass, PciAddress* out);
int pci_find_device(uint16_t vendor_id, uint16_t device_id, PciAddress* out);
uint64_t pci_bar(PciAddress addr, uint8_t bar_index);

#endif
