/*
 * BSBOOT -> kernel boot protocol.
 *
 * This header is the only contract between the bootloader and the kernel.
 * The two are built by different compilers (PE vs ELF), so every structure is
 * packed and uses fixed-width types. Addresses are 64-bit so the layout
 * survives PAE or a future 64-bit port.
 *
 * Evolving it: append fields to the end of struct bootinfo and bump
 * BOOTINFO_VERSION. Kernels must check `size` before reading newer fields.
 */
#ifndef BSHIT_BOOTINFO_H
#define BSHIT_BOOTINFO_H

#include <stdint.h>

#define BOOTINFO_MAGIC   0x49425342u /* "BSBI" */
#define BOOTINFO_VERSION 2u

enum bootinfo_mem_type {
    BOOTINFO_MEM_USABLE     = 1,
    BOOTINFO_MEM_RESERVED   = 2,
    BOOTINFO_MEM_ACPI       = 3, /* reclaimable once ACPI tables are parsed */
    BOOTINFO_MEM_BOOTLOADER = 4, /* holds the kernel and this structure */
};

struct bootinfo_mem_range {
    uint64_t base;
    uint64_t length;
    uint32_t type;
    uint32_t reserved;
} __attribute__((packed));

enum bootinfo_fb_format {
    BOOTINFO_FB_NONE = 0,
    BOOTINFO_FB_RGBX = 1, /* byte order in memory */
    BOOTINFO_FB_BGRX = 2,
};

struct bootinfo_framebuffer {
    uint64_t base;
    uint32_t width;
    uint32_t height;
    uint32_t pixels_per_line;
    uint32_t format;
} __attribute__((packed));

struct bootinfo {
    uint32_t magic;
    uint32_t version;
    uint32_t size;
    uint32_t mem_range_count;
    uint64_t mem_ranges; /* physical address of bootinfo_mem_range[] */
    uint64_t acpi_rsdp;  /* 0 if the firmware did not provide one */
    struct bootinfo_framebuffer framebuffer;
    /* Version 2: the root filesystem, when booted live from a CD. */
    uint64_t ramdisk_base;
    uint64_t ramdisk_size;
} __attribute__((packed));

_Static_assert(sizeof(struct bootinfo_mem_range) == 24, "boot ABI changed");
_Static_assert(sizeof(struct bootinfo) == 72, "boot ABI changed");

#endif
