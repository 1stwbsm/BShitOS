/* Network card drivers. To add one, implement this and list it in nic.c. */
#ifndef KERNEL_NIC_H
#define KERNEL_NIC_H

#include <stdint.h>

struct nic_driver {
    const char *name;
    int (*init)(void); /* returns 0 if its hardware was found */
    void (*mac)(uint8_t out[6]);
    void (*send)(const void *frame, uint32_t length);
    uint32_t (*receive)(void *frame); /* returns 0 if nothing is waiting */
};

extern const struct nic_driver e1000_driver, rtl8168_driver;

#endif
