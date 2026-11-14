#ifndef KERNEL_PCI_H
#define KERNEL_PCI_H

#include <stdint.h>

struct pci_device {
    uint8_t bus, dev, fn;
};

/* Both return 0 and fill *out when a matching device exists. */
int pci_find_class(uint32_t class_code, struct pci_device *out);
/* The nth device (from 0) of a class. */
int pci_find_class_nth(uint32_t class_code, unsigned int n, struct pci_device *out);
int pci_find_ids(uint16_t vendor, const uint16_t *devices, unsigned int count, struct pci_device *out);
/* Enables memory decoding and bus mastering. */
void pci_enable(const struct pci_device *device);
uint32_t pci_bar(const struct pci_device *device, unsigned int index);

#endif
