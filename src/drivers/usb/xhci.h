#ifndef XHCI_H
#define XHCI_H

#include "freelib/kstdint.h"


#define XHCI_PCI_CLASS_SERIAL 0x0C
#define XHCI_PCI_SUBCLASS_USB 0x03
#define XHCI_PCI_PROGIF_XHCI 0x30


int xhci_init(void);

#endif
