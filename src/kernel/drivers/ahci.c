/* AHCI driver: one disk and one optical drive, one command slot each, polled. */
#include "drivers/ahci.h"
#include "drivers/pci.h"
#include "lib/string.h"

#define CLASS_AHCI 0x010601u
#define AHCI_BAR 5

#define HBA_GHC 0x04
#define HBA_PI 0x0C
#define GHC_AE (1u << 31)

#define PORT_BASE(n) (0x100u + (n) * 0x80u)
#define P_CLB 0x00
#define P_CLBU 0x04
#define P_FB 0x08
#define P_FBU 0x0C
#define P_IS 0x10
#define P_IE 0x14
#define P_CMD 0x18
#define P_TFD 0x20
#define P_SIG 0x24
#define P_SSTS 0x28
#define P_SERR 0x30
#define P_CI 0x38

#define CMD_ST (1u << 0)
#define CMD_FRE (1u << 4)
#define CMD_FR (1u << 14)
#define CMD_CR (1u << 15)
#define CMD_ATAPI (1u << 24)
#define IS_TFES (1u << 30)
#define TFD_BSY 0x80u
#define TFD_DRQ 0x08u
#define TFD_ERR 0x01u
#define SSTS_ACTIVE 3u
#define SIG_ATA 0x00000101u
#define SIG_ATAPI 0xEB140101u

#define ATA_READ_DMA_EXT 0x25
#define ATA_WRITE_DMA_EXT 0x35
#define ATA_FLUSH_CACHE_EXT 0xEA
#define ATA_IDENTIFY 0xEC
#define ATA_PACKET 0xA0
#define SCSI_READ_10 0x28
#define FIS_REG_H2D 0x27
#define FIS_COMMAND_BIT 0x80
#define PACKET_FEATURE_DMA 0x01
#define HEADER_ATAPI (1u << 5)
#define HEADER_WRITE (1u << 6)
#define CFIS_DWORDS 5u
#define MAX_SECTORS 8192u
#define POLL_LIMIT 50000000u

struct command_header {
    uint32_t flags, prdbc, ctba, ctbau, reserved[4];
};

struct command_table {
    uint8_t cfis[64];
    uint8_t acmd[16];
    uint8_t reserved[48];
    struct {
        uint32_t dba, dbau, reserved, dbc;
    } prdt[1];
};

struct port {
    volatile uint8_t *regs;
    struct command_header list[32] __attribute__((aligned(1024)));
    uint8_t received_fis[256] __attribute__((aligned(256)));
    struct command_table table __attribute__((aligned(128)));
};

static struct port disk __attribute__((aligned(1024)));
static struct port cd __attribute__((aligned(1024)));

static uint32_t rd(volatile uint8_t *base, uint32_t reg)
{
    return *(volatile uint32_t *)(base + reg);
}

static void wr(volatile uint8_t *base, uint32_t reg, uint32_t value)
{
    *(volatile uint32_t *)(base + reg) = value;
}

static int wait_clear(const struct port *p, uint32_t reg, uint32_t mask)
{
    for (uint32_t i = 0; i < POLL_LIMIT; i++)
        if (!(rd(p->regs, reg) & mask))
            return 0;
    return -1;
}

static int start_port(struct port *p, volatile uint8_t *regs)
{
    p->regs = regs;

    /* The firmware left this port running against its own memory. */
    wr(regs, P_CMD, rd(regs, P_CMD) & ~(CMD_ST | CMD_FRE));
    if (wait_clear(p, P_CMD, CMD_CR | CMD_FR) < 0) {
        p->regs = 0;
        return -1;
    }

    memset(p->list, 0, sizeof(p->list));
    memset(p->received_fis, 0, sizeof(p->received_fis));
    wr(regs, P_CLB, (uint32_t)p->list);
    wr(regs, P_CLBU, 0);
    wr(regs, P_FB, (uint32_t)p->received_fis);
    wr(regs, P_FBU, 0);
    wr(regs, P_IE, 0);
    wr(regs, P_SERR, 0xFFFFFFFFu);
    wr(regs, P_IS, 0xFFFFFFFFu);

    wr(regs, P_CMD, rd(regs, P_CMD) | CMD_FRE);
    if (wait_clear(p, P_TFD, TFD_BSY | TFD_DRQ) < 0) {
        p->regs = 0;
        return -1;
    }
    wr(regs, P_CMD, rd(regs, P_CMD) | CMD_ST);
    return 0;
}

int ahci_init(void)
{
    struct pci_device device;
    volatile uint8_t *hba;
    uint32_t implemented;

    if (pci_find_class(CLASS_AHCI, &device) < 0)
        return -1;
    pci_enable(&device);
    hba = (volatile uint8_t *)(pci_bar(&device, AHCI_BAR) & ~0xFu);
    wr(hba, HBA_GHC, rd(hba, HBA_GHC) | GHC_AE);
    implemented = rd(hba, HBA_PI);

    for (uint32_t n = 0; n < 32; n++) {
        volatile uint8_t *regs = hba + PORT_BASE(n);
        uint32_t signature;

        if (!(implemented & (1u << n)) || (rd(regs, P_SSTS) & 0xF) != SSTS_ACTIVE)
            continue;
        signature = rd(regs, P_SIG);
        if (signature == SIG_ATA && !disk.regs) {
            start_port(&disk, regs);
        } else if (signature == SIG_ATAPI && !cd.regs) {
            if (start_port(&cd, regs) == 0)
                wr(regs, P_CMD, rd(regs, P_CMD) | CMD_ATAPI);
        }
    }
    return 0;
}

int ahci_has_disk(void)
{
    return disk.regs != 0;
}

int ahci_has_cd(void)
{
    return cd.regs != 0;
}

static int issue(struct port *p, uint32_t flags, uint32_t bytes, void *buf)
{
    if (!p->regs)
        return -1;

    p->list[0].flags = CFIS_DWORDS | flags | (buf ? 1u << 16 : 0);
    p->list[0].prdbc = 0;
    p->list[0].ctba = (uint32_t)&p->table;
    p->list[0].ctbau = 0;
    if (buf) {
        p->table.prdt[0].dba = (uint32_t)buf;
        p->table.prdt[0].dbau = 0;
        p->table.prdt[0].dbc = bytes - 1;
    }

    if (wait_clear(p, P_TFD, TFD_BSY | TFD_DRQ) < 0)
        return -1;
    __asm__ volatile("" ::: "memory");
    wr(p->regs, P_CI, 1);

    for (uint32_t i = 0; i < POLL_LIMIT; i++) {
        uint32_t status = rd(p->regs, P_IS);

        if (status & IS_TFES) {
            wr(p->regs, P_IS, status);
            return -1;
        }
        if (!(rd(p->regs, P_CI) & 1)) {
            wr(p->regs, P_IS, status);
            return rd(p->regs, P_TFD) & TFD_ERR ? -1 : 0;
        }
    }
    return -1;
}

static int ata_command(uint8_t command, uint64_t lba, uint32_t sectors, void *buf, int write)
{
    struct command_table *t = &disk.table;

    if (sectors > MAX_SECTORS)
        return -1;
    memset(t, 0, sizeof(*t));
    t->cfis[0] = FIS_REG_H2D;
    t->cfis[1] = FIS_COMMAND_BIT;
    t->cfis[2] = command;
    t->cfis[4] = (uint8_t)lba;
    t->cfis[5] = (uint8_t)(lba >> 8);
    t->cfis[6] = (uint8_t)(lba >> 16);
    t->cfis[7] = 0x40; /* LBA mode */
    t->cfis[8] = (uint8_t)(lba >> 24);
    t->cfis[9] = (uint8_t)(lba >> 32);
    t->cfis[10] = (uint8_t)(lba >> 40);
    t->cfis[12] = (uint8_t)sectors;
    t->cfis[13] = (uint8_t)(sectors >> 8);
    return issue(&disk, write ? HEADER_WRITE : 0, sectors * AHCI_SECTOR_SIZE, buf);
}

int ahci_read(uint64_t lba, uint32_t sectors, void *buf)
{
    return ata_command(ATA_READ_DMA_EXT, lba, sectors, buf, 0);
}

int ahci_write(uint64_t lba, uint32_t sectors, const void *buf)
{
    if (ata_command(ATA_WRITE_DMA_EXT, lba, sectors, (void *)buf, 1) < 0)
        return -1;
    return ata_command(ATA_FLUSH_CACHE_EXT, 0, 0, 0, 0);
}

int ahci_identify(uint64_t *sectors, char *model, uint32_t model_size)
{
    static uint16_t id[256] __attribute__((aligned(16)));
    struct command_table *t = &disk.table;
    uint32_t n = 0;

    memset(t, 0, sizeof(*t));
    t->cfis[0] = FIS_REG_H2D;
    t->cfis[1] = FIS_COMMAND_BIT;
    t->cfis[2] = ATA_IDENTIFY;
    if (issue(&disk, 0, sizeof(id), id) < 0)
        return -1;
    /* words 100-103: LBA48 sector count; 60-61 is the 28-bit fallback */
    *sectors = (uint64_t)id[100] | (uint64_t)id[101] << 16 | (uint64_t)id[102] << 32 | (uint64_t)id[103] << 48;
    if (*sectors == 0)
        *sectors = (uint32_t)id[60] | (uint32_t)id[61] << 16;
    /* words 27-46: the model, two characters per word, high byte first */
    for (uint32_t w = 27; w <= 46 && n + 2 < model_size; w++) {
        model[n++] = (char)(id[w] >> 8);
        model[n++] = (char)id[w];
    }
    while (n > 0 && model[n - 1] == ' ')
        n--;
    model[n] = 0;
    return 0;
}

int ahci_cd_read(uint32_t lba, uint32_t sectors, void *buf)
{
    struct command_table *t = &cd.table;

    if (sectors > MAX_SECTORS)
        return -1;
    memset(t, 0, sizeof(*t));
    t->cfis[0] = FIS_REG_H2D;
    t->cfis[1] = FIS_COMMAND_BIT;
    t->cfis[2] = ATA_PACKET;
    t->cfis[3] = PACKET_FEATURE_DMA;
    t->acmd[0] = SCSI_READ_10;
    t->acmd[2] = (uint8_t)(lba >> 24);
    t->acmd[3] = (uint8_t)(lba >> 16);
    t->acmd[4] = (uint8_t)(lba >> 8);
    t->acmd[5] = (uint8_t)lba;
    t->acmd[7] = (uint8_t)(sectors >> 8);
    t->acmd[8] = (uint8_t)sectors;
    return issue(&cd, HEADER_ATAPI, sectors * AHCI_CD_SECTOR_SIZE, buf);
}
