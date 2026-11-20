/*
 * Where BSBOOT for BIOS puts things in the first megabyte. Everything the BIOS
 * reads or writes stays below 64 KiB, so segment 0 reaches all of it.
 *
 *   0x0500  VBE controller info (512 bytes)
 *   0x0700  VBE mode info (256 bytes)
 *   0x0800  boot info and its memory map, for the kernel
 *   0x1000  disk bounce buffer (4 KiB)
 *   0x2000  stack, growing down from 0x7C00
 *   0x7C00  the boot sector
 *   0x8000  stage 2, at most 32 KiB including its BSS
 *  0x20000  the kernel file, before its segments are copied above 1 MiB
 */
#ifndef BIOS_LAYOUT_H
#define BIOS_LAYOUT_H

#define STAGE2_LBA 34          /* right after the 32 sectors of GPT entries */
#define STAGE2_SECTORS 64
#define STAGE2_ADDRESS 0x8000
#define STAGE2_SEGMENT 0x0800  /* STAGE2_ADDRESS / 16 */
#define STAGE2_END 0x10000
#define VBE_INFO 0x500
#define VBE_MODE_INFO 0x700
#define BOOTINFO_ADDRESS 0x800
#define BOOTINFO_END 0x1000
#define BOUNCE 0x1000
#define STACK_TOP 0x7C00
#define STAGING 0x20000
#define STAGING_END 0x90000

#endif
