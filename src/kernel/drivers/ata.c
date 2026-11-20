/*
 * Legacy IDE (parallel ATA), the way every PC since the AT reaches its disk:
 * I/O ports, polled, one sector of words at a time (PIO). Slow and universal.
 * Only hard disks; ATAPI drives answer IDENTIFY differently and are skipped.
 */
#include "arch/x86/cpu.h"
#include "drivers/ata.h"

#define REG_DATA 0
#define REG_ERROR 1
#define REG_COUNT 2
#define REG_LBA0 3
#define REG_LBA1 4
#define REG_LBA2 5
#define REG_DRIVE 6
#define REG_STATUS 7
#define REG_COMMAND 7
#define STATUS_ERR 0x01
#define STATUS_DRQ 0x08
#define STATUS_DF 0x20
#define STATUS_BSY 0x80
#define CONTROL_NIEN 0x02 /* no interrupts: everything here polls */
#define CMD_READ 0x20
#define CMD_READ_EXT 0x24
#define CMD_WRITE 0x30
#define CMD_WRITE_EXT 0x34
#define CMD_FLUSH 0xE7
#define CMD_FLUSH_EXT 0xEA
#define CMD_IDENTIFY 0xEC
#define WAIT_LIMIT 4000000u

static const uint16_t bases[2] = {0x1F0, 0x170}, controls[2] = {0x3F6, 0x376};

static struct {
    int found, lba48;
    uint16_t base, control;
    uint8_t select; /* 0xE0 master, 0xF0 slave: LBA mode */
    uint64_t sectors;
    char model[41];
} disk;

static inline uint16_t inw(uint16_t port)
{
    uint16_t value;
    __asm__ volatile("inw %w1, %w0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outw(uint16_t port, uint16_t value)
{
    __asm__ volatile("outw %w0, %w1" : : "a"(value), "Nd"(port));
}

static void settle(uint16_t control)
{
    for (int i = 0; i < 4; i++) /* each alternate-status read takes about 100 ns */
        inb(control);
}

static int wait_ready(uint16_t base, int want_data)
{
    for (uint32_t i = 0; i < WAIT_LIMIT; i++) {
        uint8_t status = inb((uint16_t)(base + REG_STATUS));

        if (status & STATUS_BSY)
            continue;
        if (status & (STATUS_ERR | STATUS_DF))
            return -1;
        if (!want_data || (status & STATUS_DRQ))
            return 0;
    }
    return -1;
}

static int probe(int channel, int slave)
{
    uint16_t base = bases[channel], control = controls[channel], id[256];
    uint8_t status;

    if (inb((uint16_t)(base + REG_STATUS)) == 0xFF)
        return -1; /* nothing on this channel */
    outb(control, CONTROL_NIEN);
    outb((uint16_t)(base + REG_DRIVE), (uint8_t)(0xA0 | slave << 4));
    settle(control);
    outb((uint16_t)(base + REG_COUNT), 0);
    outb((uint16_t)(base + REG_LBA0), 0);
    outb((uint16_t)(base + REG_LBA1), 0);
    outb((uint16_t)(base + REG_LBA2), 0);
    outb((uint16_t)(base + REG_COMMAND), CMD_IDENTIFY);
    status = inb((uint16_t)(base + REG_STATUS));
    if (status == 0)
        return -1; /* no drive */
    for (uint32_t i = 0; i < WAIT_LIMIT && (inb((uint16_t)(base + REG_STATUS)) & STATUS_BSY); i++)
        ;
    if (inb((uint16_t)(base + REG_LBA1)) || inb((uint16_t)(base + REG_LBA2)))
        return -1; /* ATAPI or SATA signature: not an ATA hard disk */
    if (wait_ready(base, 1) < 0)
        return -1;
    for (int i = 0; i < 256; i++)
        id[i] = inw((uint16_t)(base + REG_DATA));

    disk.base = base;
    disk.control = control;
    disk.select = (uint8_t)(0xE0 | slave << 4);
    disk.lba48 = (id[83] >> 10) & 1;
    disk.sectors = disk.lba48 ? (uint64_t)id[100] | (uint64_t)id[101] << 16 | (uint64_t)id[102] << 32 |
                                    (uint64_t)id[103] << 48
                              : (uint32_t)id[60] | (uint32_t)id[61] << 16;
    for (int w = 0; w < 20; w++) {
        disk.model[w * 2] = (char)(id[27 + w] >> 8);
        disk.model[w * 2 + 1] = (char)id[27 + w];
    }
    for (int n = 40; n > 0 && (disk.model[n - 1] == ' ' || !disk.model[n - 1]); n--)
        disk.model[n - 1] = 0;
    disk.found = 1;
    return 0;
}

int ata_init(void)
{
    for (int channel = 0; channel < 2; channel++)
        for (int slave = 0; slave < 2; slave++)
            if (probe(channel, slave) == 0)
                return 0;
    return -1;
}

int ata_present(void)
{
    return disk.found;
}

/* Sets up one command for count sectors (1..256, or 65536 with LBA48). */
static int command(uint64_t lba, uint32_t count, uint8_t cmd28, uint8_t cmd48)
{
    uint16_t base = disk.base;
    int ext = disk.lba48 && (lba + count > 0x0FFFFFFFu || count > 256);

    if (!ext && (lba + count > 0x0FFFFFFFu || count > 256))
        return -1;
    if (wait_ready(base, 0) < 0)
        return -1;
    if (ext) {
        outb((uint16_t)(base + REG_DRIVE), (uint8_t)(disk.select & 0xF0));
        outb((uint16_t)(base + REG_COUNT), (uint8_t)(count >> 8));
        outb((uint16_t)(base + REG_LBA0), (uint8_t)(lba >> 24));
        outb((uint16_t)(base + REG_LBA1), (uint8_t)(lba >> 32));
        outb((uint16_t)(base + REG_LBA2), (uint8_t)(lba >> 40));
    } else {
        outb((uint16_t)(base + REG_DRIVE), (uint8_t)(disk.select | ((lba >> 24) & 0x0F)));
    }
    outb((uint16_t)(base + REG_COUNT), (uint8_t)count);
    outb((uint16_t)(base + REG_LBA0), (uint8_t)lba);
    outb((uint16_t)(base + REG_LBA1), (uint8_t)(lba >> 8));
    outb((uint16_t)(base + REG_LBA2), (uint8_t)(lba >> 16));
    outb((uint16_t)(base + REG_COMMAND), ext ? cmd48 : cmd28);
    return 0;
}

int ata_read(uint64_t lba, uint32_t sectors, void *buf)
{
    uint16_t *p = buf;

    while (sectors) {
        uint32_t n = sectors < 256 ? sectors : 256;

        if (!disk.found || command(lba, n, CMD_READ, CMD_READ_EXT) < 0)
            return -1;
        for (uint32_t s = 0; s < n; s++) {
            if (wait_ready(disk.base, 1) < 0)
                return -1;
            for (int i = 0; i < 256; i++)
                *p++ = inw((uint16_t)(disk.base + REG_DATA));
        }
        lba += n;
        sectors -= n;
    }
    return 0;
}

int ata_flush(void)
{
    if (!disk.found || wait_ready(disk.base, 0) < 0)
        return -1;
    outb((uint16_t)(disk.base + REG_DRIVE), disk.select);
    outb((uint16_t)(disk.base + REG_COMMAND), disk.lba48 ? CMD_FLUSH_EXT : CMD_FLUSH);
    return wait_ready(disk.base, 0);
}

int ata_write(uint64_t lba, uint32_t sectors, const void *buf)
{
    const uint16_t *p = buf;

    while (sectors) {
        uint32_t n = sectors < 256 ? sectors : 256;

        if (!disk.found || command(lba, n, CMD_WRITE, CMD_WRITE_EXT) < 0)
            return -1;
        for (uint32_t s = 0; s < n; s++) {
            if (wait_ready(disk.base, 1) < 0)
                return -1;
            for (int i = 0; i < 256; i++)
                outw((uint16_t)(disk.base + REG_DATA), *p++);
        }
        lba += n;
        sectors -= n;
    }
    return ata_flush();
}

int ata_identify(uint64_t *sectors, char *model, uint32_t model_size)
{
    uint32_t n = 0;

    if (!disk.found)
        return -1;
    *sectors = disk.sectors;
    for (; disk.model[n] && n + 1 < model_size; n++)
        model[n] = disk.model[n];
    model[n] = 0;
    return 0;
}
