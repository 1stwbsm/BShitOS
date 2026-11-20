/* Routes disk I/O to the controller the hard disk was found on. */
#include "drivers/ahci.h"
#include "drivers/block.h"

static enum { NONE, SATA } kind;

int block_init(void)
{
    if (ahci_has_disk())
        kind = SATA;
    return kind == NONE ? -1 : 0;
}

int block_present(void)
{
    return kind != NONE;
}

const char *block_kind(void)
{
    return kind == SATA ? "SATA" : "none";
}

int block_read(uint64_t lba, uint32_t sectors, void *buf)
{
    return kind == SATA ? ahci_read(lba, sectors, buf) : -1;
}

int block_write(uint64_t lba, uint32_t sectors, const void *buf)
{
    return kind == SATA ? ahci_write(lba, sectors, buf) : -1;
}

int block_flush(void)
{
    return kind == SATA ? ahci_flush() : -1;
}

int block_identify(uint64_t *sectors, char *model, uint32_t model_size)
{
    return kind == SATA ? ahci_identify(sectors, model, model_size) : -1;
}
