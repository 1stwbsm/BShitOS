/*
 * The hard disk, whichever controller it hangs off: AHCI (SATA) first, then
 * legacy IDE. Sectors are 512 bytes. Buffers must be physical (kernel) memory.
 */
#ifndef KERNEL_BLOCK_H
#define KERNEL_BLOCK_H

#include <stdint.h>

#define BLOCK_SECTOR_SIZE 512u

/* Probes the controllers; returns 0 if a hard disk was found. */
int block_init(void);
int block_present(void);
const char *block_kind(void); /* "SATA" or "IDE" */
int block_read(uint64_t lba, uint32_t sectors, void *buf);
int block_write(uint64_t lba, uint32_t sectors, const void *buf);
int block_flush(void);
int block_identify(uint64_t *sectors, char *model, uint32_t model_size);

#endif
