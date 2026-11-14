#ifndef BSBOOT_ELF_H
#define BSBOOT_ELF_H

#include "efi/efi.h"

/*
 * Copies a 32-bit x86 ELF executable to the physical addresses in its program
 * headers and returns the physical entry point.
 */
EFI_STATUS elf_load(const void *image, UINTN size, UINT32 *entry);

#endif
