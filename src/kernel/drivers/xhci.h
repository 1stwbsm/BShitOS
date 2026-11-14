/* USB 3 (xHCI) host driver: keyboards, mice and mass storage, polled. */
#ifndef KERNEL_XHCI_H
#define KERNEL_XHCI_H

#include <stdint.h>

/* Starts every controller and enumerates devices on the root ports. */
void usb_init(void);
/* Delivers pending keyboard and mouse reports. */
void usb_poll(void);
int usb_storage_present(void);
int usb_storage_read(uint64_t lba, uint32_t sectors, void *buf);

#endif
