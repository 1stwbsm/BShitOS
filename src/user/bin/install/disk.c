/* Raw disk writes: the GPT and the EFI System Partition. */
#include <bshit/passhash.h>

#include "install.h"

#define GPT_ENTRIES 128u
#define GPT_ENTRY_SIZE 128u
#define GPT_ENTRY_SECTORS (GPT_ENTRIES * GPT_ENTRY_SIZE / SECTOR) /* 32 */
#define CHUNK_SECTORS BS_DISK_IO_MAX

static unsigned char chunk[CHUNK_SECTORS * SECTOR];

static const unsigned char ESP_TYPE[16] = {0x28, 0x73, 0x2A, 0xC1, 0x1F, 0xF8, 0xD2, 0x11,
                                           0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B};
static const unsigned char BSFS_TYPE[16] = {0xF5, 0xB5, 0xF5, 0xB5, 0x00, 0x00, 0x53, 0x42,
                                            0x46, 0x53, 0x42, 0x53, 0x48, 0x49, 0x54, 0x21};

int disk_write_sectors(u64 lba, const void *data, unsigned int sectors)
{
    const unsigned char *p = data;

    while (sectors) {
        unsigned int n = sectors < CHUNK_SECTORS ? sectors : CHUNK_SECTORS;
        struct bs_disk_io io = {(unsigned int)lba, (unsigned int)(lba >> 32), n, (unsigned int)p};
        int rc = disk_write(&io);

        if (rc < 0)
            return rc;
        lba += n;
        p += n * SECTOR;
        sectors -= n;
    }
    return 0;
}

static int write_zeros(u64 lba, u64 sectors)
{
    memset(chunk, 0, sizeof(chunk));
    while (sectors) {
        unsigned int n = sectors < CHUNK_SECTORS ? (unsigned int)sectors : CHUNK_SECTORS;
        int rc = disk_write_sectors(lba, chunk, n);

        if (rc < 0)
            return rc;
        lba += n;
        sectors -= n;
    }
    return 0;
}

void progress(const char *what)
{
    printf("\033[u\033[K\033[90m%s\033[0m", what);
}

void random_bytes(unsigned char *out, unsigned int count)
{
    get_random(out, count);
}

/* Reads up to size bytes of a file; returns the length, or a negative error. */
static int load_file(const char *path, void *out, unsigned int size)
{
    int fd = open(path, BS_O_READ), got;

    if (fd < 0)
        return fd;
    got = read(fd, out, size);
    close(fd);
    return got;
}

static unsigned int crc32(const unsigned char *p, unsigned int n)
{
    unsigned int crc = 0xFFFFFFFFu;

    while (n--) {
        crc ^= *p++;
        for (int k = 0; k < 8; k++)
            crc = crc >> 1 ^ (0xEDB88320u & (0u - (crc & 1)));
    }
    return ~crc;
}

static void put32(unsigned char *p, unsigned int v)
{
    p[0] = (unsigned char)v, p[1] = (unsigned char)(v >> 8), p[2] = (unsigned char)(v >> 16), p[3] = (unsigned char)(v >> 24);
}

static void put64(unsigned char *p, u64 v)
{
    put32(p, (unsigned int)v);
    put32(p + 4, (unsigned int)(v >> 32));
}

static void random_guid(unsigned char *guid)
{
    random_bytes(guid, 16);
    guid[7] = (unsigned char)((guid[7] & 0x0F) | 0x40); /* version 4, stored little-endian */
    guid[8] = (unsigned char)((guid[8] & 0x3F) | 0x80);
}

static void entry(unsigned char *e, const unsigned char *type, u64 first, u64 sectors, const char *name)
{
    memcpy(e, type, 16);
    random_guid(e + 16);
    put64(e + 32, first);
    put64(e + 40, first + sectors - 1);
    for (unsigned int i = 0; name[i] && i < 36; i++)
        e[56 + i * 2] = (unsigned char)name[i]; /* UTF-16LE */
}

static void header(unsigned char *h, u64 self, u64 other, u64 first_usable, u64 last_usable,
                   const unsigned char *guid, u64 entries_lba, unsigned int entries_crc)
{
    memset(h, 0, SECTOR);
    memcpy(h, "EFI PART", 8);
    put32(h + 8, 0x00010000);
    put32(h + 12, 92);
    put64(h + 24, self);
    put64(h + 32, other);
    put64(h + 40, first_usable);
    put64(h + 48, last_usable);
    memcpy(h + 56, guid, 16);
    put64(h + 72, entries_lba);
    put32(h + 80, GPT_ENTRIES);
    put32(h + 84, GPT_ENTRY_SIZE);
    put32(h + 88, entries_crc);
    put32(h + 16, crc32(h, 92));
}

int write_partition_table(const struct plan *plan)
{
    static unsigned char entries[GPT_ENTRY_SECTORS * SECTOR], sector[SECTOR];
    unsigned char disk_guid[16];
    u64 last = plan->disk_sectors - 1;
    u64 backup_entries = last - GPT_ENTRY_SECTORS;
    unsigned int entries_crc;
    int rc;

    memset(entries, 0, sizeof(entries));
    entry(entries, ESP_TYPE, ESP_LBA, plan->esp_sectors, "EFI System");
    entry(entries + GPT_ENTRY_SIZE, BSFS_TYPE, plan->bsfs_lba, plan->bsfs_sectors, "BShitOS");
    entries_crc = crc32(entries, sizeof(entries));
    random_guid(disk_guid);

    /* Protective MBR: one 0xEE partition covering the disk, so old tools keep off.
     * Its first 440 bytes are BSBOOT's BIOS boot sector, so the disk boots on BIOS PCs too. */
    memset(sector, 0, SECTOR);
    load_file(BIOS_BOOT_SECTOR, sector, 440);
    sector[446 + 2] = 2;
    sector[446 + 4] = 0xEE;
    sector[446 + 5] = sector[446 + 6] = sector[446 + 7] = 0xFF;
    put32(sector + 446 + 8, 1);
    put32(sector + 446 + 12, last > 0xFFFFFFFFu ? 0xFFFFFFFFu : (unsigned int)last);
    sector[510] = 0x55;
    sector[511] = 0xAA;
    rc = disk_write_sectors(0, sector, 1);

    /* Wipe whatever filesystem headers were there, then the primary and backup GPT. */
    if (rc == 0)
        rc = write_zeros(1, ESP_LBA - 1);
    if (rc == 0)
        rc = disk_write_sectors(2, entries, GPT_ENTRY_SECTORS);
    if (rc == 0) { /* BIOS stage 2, in the gap between the GPT entries and the first partition */
        int length = load_file(BIOS_STAGE2, chunk, sizeof(chunk));

        if (length > 0)
            rc = disk_write_sectors(2 + GPT_ENTRY_SECTORS, chunk, ((unsigned int)length + SECTOR - 1) / SECTOR);
    }
    if (rc == 0) {
        header(sector, 1, last, 2 + GPT_ENTRY_SECTORS, backup_entries - 1, disk_guid, 2, entries_crc);
        rc = disk_write_sectors(1, sector, 1);
    }
    if (rc == 0)
        rc = disk_write_sectors(backup_entries, entries, GPT_ENTRY_SECTORS);
    if (rc == 0) {
        header(sector, last, 1, 2 + GPT_ENTRY_SECTORS, backup_entries - 1, disk_guid, backup_entries, entries_crc);
        rc = disk_write_sectors(last, sector, 1);
    }
    return rc;
}

int esp_size(u64 *sectors)
{
    unsigned char boot[SECTOR];
    int fd = open(ESP_SOURCE, BS_O_READ), got;

    if (fd < 0)
        return fd;
    got = read(fd, boot, SECTOR);
    close(fd);
    if (got != (int)SECTOR || boot[510] != 0x55 || boot[511] != 0xAA)
        return -BS_EINVAL;
    *sectors = (unsigned int)boot[32] | (unsigned int)boot[33] << 8 | (unsigned int)boot[34] << 16 |
               (unsigned int)boot[35] << 24; /* BPB_TotSec32 */
    return *sectors ? 0 : -BS_EINVAL;
}

/* ESP.IMG has its trailing zeros cut off; the rest of the partition is zeroed. */
int copy_esp(const struct plan *plan)
{
    u64 written = 0;
    int fd = open(ESP_SOURCE, BS_O_READ), got, rc = 0;

    if (fd < 0)
        return fd;
    while (written < plan->esp_sectors && (got = read(fd, chunk, sizeof(chunk))) > 0) {
        unsigned int sectors = ((unsigned int)got + SECTOR - 1) / SECTOR;

        memset(chunk + got, 0, sectors * SECTOR - (unsigned int)got);
        if (written + sectors > plan->esp_sectors) {
            rc = -BS_ENOSPC;
            break;
        }
        rc = disk_write_sectors(ESP_LBA + written, chunk, sectors);
        if (rc < 0)
            break;
        written += sectors;
    }
    close(fd);
    if (rc == 0 && written == 0)
        rc = -BS_EIO;
    if (rc == 0)
        rc = write_zeros(ESP_LBA + written, plan->esp_sectors - written);
    return rc;
}
