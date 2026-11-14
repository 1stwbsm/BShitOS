/*
 * The machine: does what BSBOOT does on real hardware (loads the kernel and
 * hands it the boot information), then runs the processor in time slices.
 */
#include <bshit/bootinfo.h>

#include "emu.h"

#define KERNEL_MAX (1u << 20)
#define BOOTINFO_ADDRESS 0x8000u
#define STACK_TOP 0x7000u
#define RAMDISK_BASE 0x01000000u
#define LOW_MEMORY 0x00100000u
#define PAGE 4096u
#define SLICE 20000u
#define MAX_RANGES 8

static u8 staging[KERNEL_MAX];
static int last_status;

#ifdef __wasm__
/* The compiler may emit calls to these. */
void *memset(void *dst, int c, unsigned long n)
{
    u8 *d = dst;

    while (n--)
        *d++ = (u8)c;
    return dst;
}

void *memcpy(void *dst, const void *src, unsigned long n)
{
    u8 *d = dst;
    const u8 *s = src;

    while (n--)
        *d++ = *s++;
    return dst;
}
#endif

EXPORT("emu_staging") u32 emu_staging(void) { return (u32)(unsigned long)staging; }
EXPORT("emu_ram") u32 emu_ram(void) { return (u32)(unsigned long)ram; }
EXPORT("emu_framebuffer") u32 emu_framebuffer(void) { return (u32)(unsigned long)framebuffer; }
EXPORT("emu_ramdisk_base") u32 emu_ramdisk_base(void) { return RAMDISK_BASE; }
EXPORT("emu_crash_message") u32 emu_crash_message(void) { return (u32)(unsigned long)cpu_crash_message(); }

static u32 read32(const u8 *p)
{
    return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}

static u32 read16(const u8 *p)
{
    return (u32)p[0] | (u32)p[1] << 8;
}

/* Copies the kernel's PT_LOAD segments to their physical addresses. */
static int load_kernel(u32 length, u32 *entry, u32 *end)
{
    u32 phoff, count, entry_virtual;

    if (length < 52 || length > KERNEL_MAX || read32(staging) != 0x464C457Fu || staging[4] != 1)
        return -1;
    entry_virtual = read32(staging + 24);
    phoff = read32(staging + 28);
    count = read16(staging + 44);
    *entry = 0;
    *end = LOW_MEMORY;

    for (u32 i = 0; i < count; i++) {
        const u8 *ph = staging + phoff + i * 32;
        u32 offset = read32(ph + 4), vaddr = read32(ph + 8), paddr = read32(ph + 12);
        u32 filesz = read32(ph + 16), memsz = read32(ph + 20);

        if (phoff + i * 32 + 32 > length)
            return -1;
        if (read32(ph) != 1 || memsz == 0)
            continue;
        if (offset + filesz > length || paddr < LOW_MEMORY || paddr + memsz > RAMDISK_BASE)
            return -1;
        for (u32 b = 0; b < memsz; b++)
            ram[paddr + b] = b < filesz ? staging[offset + b] : 0;
        if (entry_virtual >= vaddr && entry_virtual < vaddr + memsz)
            *entry = entry_virtual - vaddr + paddr;
        if (paddr + memsz > *end)
            *end = paddr + memsz;
    }
    return *entry ? 0 : -1;
}

static void add_range(struct bootinfo_mem_range *ranges, u32 *count, u32 base, u32 end, u32 type)
{
    if (end <= base)
        return;
    ranges[*count].base = base;
    ranges[*count].length = end - base;
    ranges[*count].type = type;
    ranges[*count].reserved = 0;
    (*count)++;
}

/* kernel_length bytes are in staging; ramdisk_length bytes at RAMDISK_BASE. */
EXPORT("emu_boot") int emu_boot(u32 kernel_length, u32 ramdisk_length)
{
    struct bootinfo *info = (struct bootinfo *)(ram + BOOTINFO_ADDRESS);
    struct bootinfo_mem_range *ranges = (struct bootinfo_mem_range *)(info + 1);
    u32 entry, kernel_end, count = 0, ramdisk_end;

    if (RAMDISK_BASE + ramdisk_length > RAM_SIZE)
        return -2;
    for (u32 i = 0; i < RAMDISK_BASE; i++)
        ram[i] = 0;
    if (load_kernel(kernel_length, &entry, &kernel_end) < 0)
        return -1;

    kernel_end = (kernel_end + PAGE - 1) & ~(PAGE - 1);
    ramdisk_end = (RAMDISK_BASE + ramdisk_length + PAGE - 1) & ~(PAGE - 1);
    add_range(ranges, &count, 0, LOW_MEMORY, BOOTINFO_MEM_RESERVED);
    add_range(ranges, &count, LOW_MEMORY, kernel_end, BOOTINFO_MEM_BOOTLOADER);
    add_range(ranges, &count, kernel_end, RAMDISK_BASE, BOOTINFO_MEM_USABLE);
    add_range(ranges, &count, RAMDISK_BASE, ramdisk_end, BOOTINFO_MEM_BOOTLOADER);
    add_range(ranges, &count, ramdisk_end, RAM_SIZE, BOOTINFO_MEM_USABLE);

    info->magic = BOOTINFO_MAGIC;
    info->version = BOOTINFO_VERSION;
    info->size = sizeof(*info);
    info->mem_range_count = count;
    info->mem_ranges = BOOTINFO_ADDRESS + sizeof(*info);
    info->acpi_rsdp = 0;
    info->framebuffer.base = FB_BASE;
    info->framebuffer.width = FB_WIDTH;
    info->framebuffer.height = FB_HEIGHT;
    info->framebuffer.pixels_per_line = FB_WIDTH;
    info->framebuffer.format = BOOTINFO_FB_RGBX;
    info->ramdisk_base = RAMDISK_BASE;
    info->ramdisk_size = ramdisk_length;

    /* The kernel is entered like a cdecl function: _start(struct bootinfo *). */
    phys_write(STACK_TOP - 8, 0, 4);
    phys_write(STACK_TOP - 4, BOOTINFO_ADDRESS, 4);

    for (u32 i = 0; i < FB_WIDTH * FB_HEIGHT; i++)
        framebuffer[i] = 0xFF000000u;
    framebuffer_dirty = 1;
    devices_reset();
    cpu_reset(entry, STACK_TOP - 8);
    last_status = CPU_RUNNING;
    return 0;
}

/* Runs for about budget_ms. Returns an enum cpu_status. */
EXPORT("emu_run") int emu_run(double budget_ms)
{
    double end = host_now() + budget_ms;

    if (last_status != CPU_RUNNING)
        return last_status;
    do {
        last_status = cpu_run(SLICE);
    } while (last_status == CPU_RUNNING && host_now() < end);
    return last_status;
}

EXPORT("emu_dirty") int emu_dirty(void)
{
    int dirty = framebuffer_dirty;

    framebuffer_dirty = 0;
    return dirty;
}

EXPORT("emu_key") void emu_key(u32 code, int down) { ps2_key(code, down); }
EXPORT("emu_mouse") void emu_mouse(s32 dx, s32 dy, u32 buttons) { ps2_mouse(dx, dy, buttons); }
