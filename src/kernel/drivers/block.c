/* Routes disk I/O to the controller the hard disk was found on. */
#include "drivers/ahci.h"
#include "drivers/ata.h"
#include "drivers/block.h"

static enum { NONE, SATA, IDE } kind;

int block_init(void)
{
    if (ahci_has_disk())
        kind = SATA;
    else if (ata_init() == 0)
        kind = IDE;
    return kind == NONE ? -1 : 0;
}

int block_present(void)
{
    return kind != NONE;
}

const char *block_kind(void)
{
    return kind == SATA ? "SATA" : kind == IDE ? "IDE" : "none";
}

int block_read(uint64_t lba, uint32_t sectors, void *buf)
{
    return kind == SATA ? ahci_read(lba, sectors, buf) : kind == IDE ? ata_read(lba, sectors, buf) : -1;
}

int block_write(uint64_t lba, uint32_t sectors, const void *buf)
{
    return kind == SATA ? ahci_write(lba, sectors, buf) : kind == IDE ? ata_write(lba, sectors, buf) : -1;
}

int block_flush(void)
{
    return kind == SATA ? ahci_flush() : kind == IDE ? ata_flush() : -1;
}

int block_identify(uint64_t *sectors, char *model, uint32_t model_size)
{
    return kind == SATA ? ahci_identify(sectors, model, model_size)
           : kind == IDE ? ata_identify(sectors, model, model_size)
                         : -1;
}
