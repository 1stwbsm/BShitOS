#include "drivers/ahci.h"
#include "fs/partitions.h"
#include "lib/string.h"

#define HEADER_LBA 1
#define ENTRY_SIZE_MAX 512u

struct gpt_header {
    char signature[8];
    uint32_t revision, header_size, crc, reserved;
    uint64_t current, backup, first_usable, last_usable;
    uint8_t disk_guid[16];
    uint64_t entries_lba;
    uint32_t entry_count, entry_size, entries_crc;
} __attribute__((packed));

static const uint8_t esp_type[16] = {
    0x28, 0x73, 0x2A, 0xC1, 0x1F, 0xF8, 0xD2, 0x11, 0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B,
};
static const uint8_t bsfs_type[16] = {
    0xF5, 0xB5, 0xF5, 0xB5, 0x00, 0x00, 0x53, 0x42, 0x46, 0x53, 0x42, 0x53, 0x48, 0x49, 0x54, 0x21,
};

static int find(const uint8_t *type, uint64_t *lba)
{
    uint8_t sector[AHCI_SECTOR_SIZE] __attribute__((aligned(16)));
    struct gpt_header header;

    if (ahci_read(HEADER_LBA, 1, sector) < 0)
        return -1;
    memcpy(&header, sector, sizeof(header));
    if (memcmp(header.signature, "EFI PART", 8) != 0 || header.entry_size == 0 ||
        header.entry_size > ENTRY_SIZE_MAX)
        return -1;

    for (uint32_t i = 0; i < header.entry_count; i++) {
        uint32_t offset = i * header.entry_size;

        if (ahci_read(header.entries_lba + offset / AHCI_SECTOR_SIZE, 1, sector) < 0)
            return -1;
        offset %= AHCI_SECTOR_SIZE;
        if (memcmp(sector + offset, type, 16) == 0) {
            memcpy(lba, sector + offset + 32, sizeof(*lba));
            return 0;
        }
    }
    return -1;
}

int gpt_find_esp(uint64_t *lba)
{
    return find(esp_type, lba);
}

int gpt_find_bsfs(uint64_t *lba)
{
    return find(bsfs_type, lba);
}
