#ifndef BSBOOT_ROOTFS_H
#define BSBOOT_ROOTFS_H

#include "efi/efi.h"

/*
 * Reads a file from the first BSFS partition found, or else the live image. path is absolute. *data
 * comes from the pool; release it with gBS->FreePool.
 */
EFI_STATUS rootfs_load(const char *path, void **data, UINTN *size);

/* The live root in memory, or a null base when booting from a partition. */
void rootfs_ramdisk(void **base, UINTN *size);
BOOLEAN rootfs_ramdisk_loaded(void);

#endif
