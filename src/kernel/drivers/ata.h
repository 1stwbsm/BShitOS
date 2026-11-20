/* Legacy IDE hard disks, polled PIO. */
#ifndef KERNEL_ATA_H
#define KERNEL_ATA_H

#include <stdint.h>

int ata_init(void); /* 0 if an ATA hard disk was found on the legacy ports */
int ata_present(void);
int ata_read(uint64_t lba, uint32_t sectors, void *buf);
int ata_write(uint64_t lba, uint32_t sectors, const void *buf);
int ata_flush(void);
int ata_identify(uint64_t *sectors, char *model, uint32_t model_size);

#endif
