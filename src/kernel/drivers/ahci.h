/* AHCI SATA: hard disks, SSDs, and CD/DVD drives (ATAPI). */
#ifndef KERNEL_AHCI_H
#define KERNEL_AHCI_H

#include <stdint.h>

#define AHCI_SECTOR_SIZE 512u
#define AHCI_CD_SECTOR_SIZE 2048u

/* Probes every port. Returns 0 if the controller was found. */
int ahci_init(void);
int ahci_has_disk(void);
int ahci_has_cd(void);
int ahci_read(uint64_t lba, uint32_t sectors, void *buf);
int ahci_write(uint64_t lba, uint32_t sectors, const void *buf);
int ahci_flush(void);
/* The disk's size in sectors and its model name (IDENTIFY DEVICE). */
int ahci_identify(uint64_t *sectors, char *model, uint32_t model_size);
int ahci_cd_read(uint32_t lba, uint32_t sectors, void *buf);

#endif
