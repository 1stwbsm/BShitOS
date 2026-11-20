/*
 * BSBOOT for BIOS, stage 2. Does on a BIOS PC what BSBOOT does under UEFI:
 * finds the BSFS partition in the GPT, loads /boot/bskern.elf above 1 MiB,
 * collects the memory map, picks a screen and enters the kernel with the same
 * boot info. Built with gcc -m16; runs in unreal mode (see entry.S).
 *
 * The screen is VGA text unless the machine has the RAM for BDeskUI and a VESA
 * card with a 32-bit linear framebuffer.
 */
#include <bshit/bootinfo.h>
#include <bshit/bsfs.h>
#include <stdint.h>

#include "layout.h"

#define SECTOR 512u
#define GRAPHICS_MIN_MIB 16u /* below this, BDeskUI would not fit: stay in text mode */
#define MAX_RANGES 40u

struct bios_regs {
    uint32_t eax, ebx, ecx, edx, esi, edi, eflags;
};
#define CARRY 1u

void bios_call(int number, struct bios_regs *r);
void enter_kernel(uint32_t entry, uint32_t bootinfo) __attribute__((noreturn));

/* ---- Freestanding basics -------------------------------------------------- */

/*
 * Copies with 32-bit addresses (addr32): a plain movsb in 16-bit code uses SI
 * and DI and wraps at 64 KiB. The compiler is told to call these for every
 * block copy (-mstringop-strategy=libcall) rather than inline its own.
 */
void *memcpy(void *dst, const void *src, unsigned int n)
{
    void *d = dst;

    __asm__ volatile("addr32 rep movsb" : "+D"(d), "+S"(src), "+c"(n) : : "memory");
    return dst;
}

void *memset(void *dst, int c, unsigned int n)
{
    void *d = dst;

    __asm__ volatile("addr32 rep stosb" : "+D"(d), "+c"(n) : "a"(c) : "memory");
    return dst;
}

static int same(const void *a, const void *b, unsigned int n)
{
    const uint8_t *x = a, *y = b;

    while (n--)
        if (*x++ != *y++)
            return 0;
    return 1;
}

static inline void outb(uint16_t port, uint8_t value)
{
    __asm__ volatile("outb %b0, %w1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t value;
    __asm__ volatile("inb %w1, %b0" : "=a"(value) : "Nd"(port));
    return value;
}

static void put(const char *s)
{
    struct bios_regs r;

    for (; *s; s++) {
        if (*s == '\n')
            put("\r");
        memset(&r, 0, sizeof(r));
        r.eax = 0x0E00 | (uint8_t)*s;
        r.ebx = 0x0007;
        bios_call(0x10, &r);
    }
}

static void put_number(uint32_t value)
{
    char digits[12], *p = digits + sizeof(digits) - 1;

    *p = 0;
    do {
        *--p = (char)('0' + value % 10);
        value /= 10;
    } while (value);
    put(p);
}

static void __attribute__((noreturn)) fail(const char *why)
{
    put("\nBSBOOT: ");
    put(why);
    put("\n");
    for (;;)
        __asm__ volatile("hlt");
}

/*
 * A pointer the compiler cannot see the value of. In 16-bit code the assembler
 * encodes a constant address as 16 bits, silently dropping the top: anything
 * above 64 KiB must be reached through a register instead.
 */
static void *far_pointer(uint32_t address)
{
    __asm__("" : "+r"(address));
    return (void *)address;
}

/* ---- The disk: INT 13h, LBA where the BIOS can, cylinder/head/sector where not -- */

static uint8_t drive;
static int lba_reads;
static uint32_t sectors_per_track, heads;

static void disk_init(void)
{
    struct bios_regs r;

    memset(&r, 0, sizeof(r));
    r.eax = 0x4100;
    r.ebx = 0x55AA;
    r.edx = drive;
    bios_call(0x13, &r);
    lba_reads = !(r.eflags & CARRY) && (r.ebx & 0xFFFF) == 0xAA55 && (r.ecx & 1);
    if (lba_reads)
        return;
    memset(&r, 0, sizeof(r));
    r.eax = 0x0800;
    r.edx = drive;
    bios_call(0x13, &r);
    if (r.eflags & CARRY)
        fail("the BIOS knows nothing about this disk");
    sectors_per_track = r.ecx & 0x3F;
    heads = ((r.edx >> 8) & 0xFF) + 1;
}

/* Reads count (1..8) sectors at lba into the bounce buffer. */
static void read_to_bounce(uint32_t lba, uint32_t count)
{
    static struct {
        uint8_t size, zero;
        uint16_t count, offset, segment;
        uint32_t lba_low, lba_high;
    } __attribute__((packed, aligned(4))) dap;
    struct bios_regs r;

    if (lba_reads) {
        dap.size = 16;
        dap.count = (uint16_t)count;
        dap.offset = BOUNCE;
        dap.segment = 0;
        dap.lba_low = lba;
        dap.lba_high = 0;
        memset(&r, 0, sizeof(r));
        r.eax = 0x4200;
        r.edx = drive;
        r.esi = (uint32_t)&dap;
        bios_call(0x13, &r);
        if (r.eflags & CARRY)
            fail("disk read failed");
        return;
    }
    for (uint32_t i = 0; i < count; i++, lba++) {
        uint32_t sector = lba % sectors_per_track + 1, track = lba / sectors_per_track;
        uint32_t head = track % heads, cylinder = track / heads;

        memset(&r, 0, sizeof(r));
        r.eax = 0x0201;
        r.ebx = BOUNCE + i * SECTOR;
        r.ecx = (cylinder & 0xFF) << 8 | (cylinder >> 8 & 3) << 6 | sector;
        r.edx = head << 8 | drive;
        bios_call(0x13, &r);
        if (r.eflags & CARRY)
            fail("disk read failed (CHS)");
    }
}

/* ---- The GPT and BSFS ---------------------------------------------------------- */

static const uint8_t BSFS_TYPE[16] = {0xF5, 0xB5, 0xF5, 0xB5, 0x00, 0x00, 0x53, 0x42,
                                      0x46, 0x53, 0x42, 0x53, 0x48, 0x49, 0x54, 0x21};
static uint32_t partition_lba;

static uint32_t read32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void find_bsfs(void)
{
    const uint8_t *sector = (const uint8_t *)BOUNCE;
    uint32_t entries_lba, count, size;

    read_to_bounce(1, 1);
    if (!same(sector, "EFI PART", 8))
        fail("no GPT on this disk");
    entries_lba = read32(sector + 72);
    count = read32(sector + 80);
    size = read32(sector + 84);
    if (size < 128 || size > SECTOR || count > 1024)
        fail("odd GPT");
    for (uint32_t i = 0; i < count; i++) {
        uint32_t offset = i * size;

        read_to_bounce(entries_lba + offset / SECTOR, 1);
        if (same(sector + offset % SECTOR, BSFS_TYPE, 16)) {
            partition_lba = read32(sector + offset % SECTOR + 32);
            return;
        }
    }
    fail("no BSFS partition");
}

static int read_block(void *ctx, uint32_t block, void *buf)
{
    (void)ctx;
    read_to_bounce(partition_lba + block * (BSFS_BLOCK_SIZE / SECTOR), BSFS_BLOCK_SIZE / SECTOR);
    memcpy(buf, (const void *)BOUNCE, BSFS_BLOCK_SIZE);
    return 0;
}

static uint32_t load_kernel_file(void)
{
    static struct bsfs fs;
    struct bsfs_inode in;
    uint32_t ino;

    fs.read_block = read_block;
    if (bsfs_mount(&fs) < 0 || bsfs_read_inode(&fs, fs.sb.root_ino, &in) < 0)
        fail("BSFS will not mount");
    if (bsfs_lookup(&fs, &in, "boot", &ino) != 1 || bsfs_read_inode(&fs, ino, &in) < 0 ||
        bsfs_lookup(&fs, &in, "bskern.elf", &ino) != 1 || bsfs_read_inode(&fs, ino, &in) < 0)
        fail("no /boot/bskern.elf");
    if (in.size > STAGING_END - STAGING)
        fail("the kernel is too big for this loader");
    if (bsfs_read(&fs, &in, 0, far_pointer(STAGING), (uint32_t)in.size) != (int)in.size)
        fail("could not read the kernel");
    return (uint32_t)in.size;
}

/* Copies PT_LOAD segments to their physical addresses; returns the entry point. */
static uint32_t place_kernel(uint32_t length, uint32_t *low, uint32_t *high)
{
    const uint8_t *elf = far_pointer(STAGING);
    uint32_t phoff = read32(elf + 28), count = elf[44] | elf[45] << 8, entry_virtual = read32(elf + 24), entry = 0;

    if (length < 52 || !same(elf, "\177ELF", 4) || elf[4] != 1)
        fail("/boot/bskern.elf is not a 32-bit ELF");
    *low = 0xFFFFFFFFu;
    *high = 0;
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t *ph = elf + phoff + i * 32;
        uint32_t offset = read32(ph + 4), vaddr = read32(ph + 8), paddr = read32(ph + 12);
        uint32_t filesz = read32(ph + 16), memsz = read32(ph + 20);

        if (read32(ph) != 1 || memsz == 0)
            continue;
        if (paddr < 0x100000 || offset + filesz > length)
            fail("bad kernel segment");
        memcpy((void *)paddr, elf + offset, filesz);
        memset((void *)(paddr + filesz), 0, memsz - filesz);
        if (entry_virtual >= vaddr && entry_virtual < vaddr + memsz)
            entry = entry_virtual - vaddr + paddr;
        if (paddr < *low)
            *low = paddr;
        if (paddr + memsz > *high)
            *high = paddr + memsz;
    }
    if (!entry)
        fail("the kernel has no entry point");
    return entry;
}

/* ---- A20: the 21st address line, off on PCs for 8086 compatibility ------------- */

static int a20_enabled(void)
{
    volatile uint32_t *low = (volatile uint32_t *)0x7DF0, *high = far_pointer(0x107DF0);
    uint32_t saved = *low;
    int on;

    *low = 0x5A5A1234;
    *high = 0xA5A54321;
    on = *low == 0x5A5A1234;
    *low = saved;
    return on;
}

static void a20_enable(void)
{
    struct bios_regs r;

    if (a20_enabled())
        return;
    memset(&r, 0, sizeof(r));
    r.eax = 0x2401; /* ask the BIOS */
    bios_call(0x15, &r);
    if (a20_enabled())
        return;
    outb(0x92, (uint8_t)((inb(0x92) | 2) & ~1)); /* the "fast A20" port */
    if (a20_enabled())
        return;
    while (inb(0x64) & 2) /* the keyboard controller, the original way */
        ;
    outb(0x64, 0xD1);
    while (inb(0x64) & 2)
        ;
    outb(0x60, 0xDF);
    for (int i = 0; i < 100000 && !a20_enabled(); i++)
        ;
    if (!a20_enabled())
        fail("could not enable A20");
}

/* ---- Memory map: E820, else E801, else the 1986 call --------------------------- */

static void add_range(struct bootinfo *info, uint64_t base, uint64_t length, uint32_t type)
{
    struct bootinfo_mem_range *ranges = (struct bootinfo_mem_range *)(uint32_t)info->mem_ranges;

    if (info->mem_range_count >= MAX_RANGES || length == 0)
        return;
    ranges[info->mem_range_count].base = base;
    ranges[info->mem_range_count].length = length;
    ranges[info->mem_range_count].type = type;
    ranges[info->mem_range_count].reserved = 0;
    info->mem_range_count++;
}

static uint32_t memory_map(struct bootinfo *info)
{
    static struct {
        uint64_t base, length;
        uint32_t type, extended;
    } __attribute__((packed)) entry;
    struct bios_regs r;
    uint32_t continuation = 0, usable_kib = 0;

    do {
        memset(&r, 0, sizeof(r));
        entry.extended = 1;
        r.eax = 0xE820;
        r.ebx = continuation;
        r.ecx = sizeof(entry);
        r.edx = 0x534D4150; /* "SMAP" */
        r.edi = (uint32_t)&entry;
        bios_call(0x15, &r);
        if ((r.eflags & CARRY) || r.eax != 0x534D4150)
            break;
        add_range(info, entry.base, entry.length,
                  entry.type == 1 ? BOOTINFO_MEM_USABLE : entry.type == 3 ? BOOTINFO_MEM_ACPI : BOOTINFO_MEM_RESERVED);
        if (entry.type == 1 && entry.base >= 0x100000)
            usable_kib += (uint32_t)(entry.length >> 10);
        continuation = r.ebx;
    } while (continuation);
    if (info->mem_range_count)
        return usable_kib;

    memset(&r, 0, sizeof(r));
    r.eax = 0xE801;
    bios_call(0x15, &r);
    if (!(r.eflags & CARRY)) {
        uint32_t below16 = (r.ecx & 0xFFFF) ? r.ecx & 0xFFFF : r.eax & 0xFFFF; /* KiB from 1 MiB */
        uint32_t above16 = (r.edx & 0xFFFF) ? r.edx & 0xFFFF : r.ebx & 0xFFFF; /* 64 KiB blocks */

        add_range(info, 0x100000, (uint64_t)below16 << 10, BOOTINFO_MEM_USABLE);
        add_range(info, 0x1000000, (uint64_t)above16 << 16, BOOTINFO_MEM_USABLE);
        return below16 + above16 * 64;
    }
    memset(&r, 0, sizeof(r));
    r.eax = 0x8800;
    bios_call(0x15, &r);
    if (r.eflags & CARRY)
        fail("the BIOS will not say how much memory there is");
    add_range(info, 0x100000, (uint64_t)(r.eax & 0xFFFF) << 10, BOOTINFO_MEM_USABLE);
    return r.eax & 0xFFFF;
}

/* ---- ACPI: the RSDP is in the EBDA's first KiB or in the BIOS area ------------- */

static uint32_t find_rsdp(void)
{
    uint32_t ebda = (uint32_t)*(volatile uint16_t *)0x40E << 4;
    uint32_t areas[2][2] = {{ebda, ebda + 1024}, {0xE0000, 0x100000}};

    for (int a = 0; a < 2; a++) {
        for (uint32_t p = areas[a][0]; p + 20 <= areas[a][1]; p += 16) {
            uint8_t sum = 0;

            if (!p || !same((const void *)p, "RSD PTR ", 8))
                continue;
            for (int i = 0; i < 20; i++)
                sum = (uint8_t)(sum + ((const uint8_t *)p)[i]);
            if (sum == 0)
                return p;
        }
    }
    return 0;
}

/* ---- The screen ---------------------------------------------------------------- */

static int pick_vbe_mode(struct bootinfo_framebuffer *fb)
{
    static const uint16_t wanted[][2] = {{1280, 800}, {1280, 1024}, {1280, 960}, {1024, 768}, {800, 600}, {640, 480}};
    uint8_t *info = (uint8_t *)VBE_INFO, *mode = (uint8_t *)VBE_MODE_INFO;
    struct bios_regs r;
    uint32_t list;
    int best = -1, best_rank = 99;

    memcpy(info, "VBE2", 4);
    memset(&r, 0, sizeof(r));
    r.eax = 0x4F00;
    r.edi = VBE_INFO;
    bios_call(0x10, &r);
    if ((r.eax & 0xFFFF) != 0x004F || !same(info, "VESA", 4) || info[5] < 2)
        return -1;
    list = (uint32_t)(info[16] | info[17] << 8) + ((uint32_t)(info[18] | info[19] << 8) << 4);

    for (const uint16_t *m = (const uint16_t *)list; *m != 0xFFFF; m++) {
        uint16_t width, height;

        memset(&r, 0, sizeof(r));
        r.eax = 0x4F01;
        r.ecx = *m;
        r.edi = VBE_MODE_INFO;
        bios_call(0x10, &r);
        if ((r.eax & 0xFFFF) != 0x004F)
            continue;
        /* supported, graphics, linear framebuffer; 32 bits per pixel, direct colour */
        if ((mode[0] & 0x99) != 0x99 || mode[25] != 32 || mode[27] != 6)
            continue;
        width = (uint16_t)(mode[18] | mode[19] << 8);
        height = (uint16_t)(mode[20] | mode[21] << 8);
        for (int rank = 0; rank < (int)(sizeof(wanted) / sizeof(wanted[0])); rank++)
            if (width == wanted[rank][0] && height == wanted[rank][1] && rank < best_rank) {
                best_rank = rank;
                best = *m;
            }
    }
    if (best < 0)
        return -1;
    memset(&r, 0, sizeof(r));
    r.eax = 0x4F01;
    r.ecx = (uint32_t)best;
    r.edi = VBE_MODE_INFO;
    bios_call(0x10, &r);
    fb->base = read32(mode + 40);
    fb->width = (uint32_t)(mode[18] | mode[19] << 8);
    fb->height = (uint32_t)(mode[20] | mode[21] << 8);
    fb->pixels_per_line = (uint32_t)(mode[16] | mode[17] << 8) / 4;
    fb->format = mode[32] == 16 ? BOOTINFO_FB_BGRX : BOOTINFO_FB_RGBX; /* red's bit position */
    memset(&r, 0, sizeof(r));
    r.eax = 0x4F02;
    r.ebx = (uint32_t)best | 0x4000; /* linear framebuffer */
    bios_call(0x10, &r);
    return (r.eax & 0xFFFF) == 0x004F ? 0 : -1;
}

static void text_mode(struct bootinfo_framebuffer *fb)
{
    fb->base = 0xB8000;
    fb->width = 80;
    fb->height = 25;
    fb->pixels_per_line = 80;
    fb->format = BOOTINFO_FB_TEXT;
}

void stage2_main(uint32_t boot_drive)
{
    struct bootinfo *info = (struct bootinfo *)BOOTINFO_ADDRESS;
    uint32_t length, entry, low, high, memory_kib;

    drive = (uint8_t)boot_drive;
    put("BSBOOT/BIOS ");
    a20_enable();
    disk_init();
    put(lba_reads ? "disk:lba " : "disk:chs ");
    find_bsfs();
    length = load_kernel_file();
    entry = place_kernel(length, &low, &high);
    put("kernel:");
    put_number(length >> 10);
    put("KiB ");

    memset(info, 0, sizeof(*info));
    info->magic = BOOTINFO_MAGIC;
    info->version = BOOTINFO_VERSION;
    info->size = sizeof(*info);
    info->mem_ranges = BOOTINFO_ADDRESS + sizeof(*info);
    memory_kib = memory_map(info);
    add_range(info, low, high - low, BOOTINFO_MEM_BOOTLOADER); /* the kernel itself */
    info->acpi_rsdp = find_rsdp();
    put("memory:");
    put_number(memory_kib >> 10);
    put("MiB\n");

    if (memory_kib >> 10 < GRAPHICS_MIN_MIB || pick_vbe_mode(&info->framebuffer) < 0) {
        struct bios_regs r;

        memset(&r, 0, sizeof(r));
        r.eax = 0x0003; /* 80x25 colour text */
        bios_call(0x10, &r);
        text_mode(&info->framebuffer);
    }
    enter_kernel(entry, BOOTINFO_ADDRESS);
}
