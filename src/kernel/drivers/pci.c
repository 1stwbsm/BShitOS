#include "arch/x86/cpu.h"
#include "drivers/pci.h"

#define PCI_ADDR 0xCF8
#define PCI_DATA 0xCFC
#define REG_IDS 0x00
#define REG_COMMAND 0x04
#define REG_CLASS 0x08
#define REG_BAR0 0x10
#define CMD_MEMORY 0x02u
#define CMD_BUS_MASTER 0x04u

static uint32_t read_config(uint32_t bus, uint32_t dev, uint32_t fn, uint32_t reg)
{
    outl(PCI_ADDR, 0x80000000u | bus << 16 | dev << 11 | fn << 8 | (reg & 0xFC));
    return inl(PCI_DATA);
}

static void write_config(uint32_t bus, uint32_t dev, uint32_t fn, uint32_t reg, uint32_t value)
{
    outl(PCI_ADDR, 0x80000000u | bus << 16 | dev << 11 | fn << 8 | (reg & 0xFC));
    outl(PCI_DATA, value);
}

static int find(int (*matches)(uint32_t ids, uint32_t class_reg, const void *ctx), const void *ctx,
                struct pci_device *out)
{
    for (uint32_t bus = 0; bus < 256; bus++) {
        for (uint32_t dev = 0; dev < 32; dev++) {
            for (uint32_t fn = 0; fn < 8; fn++) {
                uint32_t ids = read_config(bus, dev, fn, REG_IDS);

                if ((ids & 0xFFFF) == 0xFFFF)
                    continue;
                if (matches(ids, read_config(bus, dev, fn, REG_CLASS), ctx)) {
                    out->bus = (uint8_t)bus;
                    out->dev = (uint8_t)dev;
                    out->fn = (uint8_t)fn;
                    return 0;
                }
            }
        }
    }
    return -1;
}

struct class_query {
    uint32_t class_code;
    unsigned int skip;
};

static int match_class(uint32_t ids, uint32_t class_reg, const void *ctx)
{
    struct class_query *query = (struct class_query *)ctx;

    (void)ids;
    if (class_reg >> 8 != query->class_code)
        return 0;
    if (query->skip) {
        query->skip--;
        return 0;
    }
    return 1;
}

int pci_find_class_nth(uint32_t class_code, unsigned int n, struct pci_device *out)
{
    struct class_query query = {class_code, n};

    return find(match_class, &query, out);
}

int pci_find_class(uint32_t class_code, struct pci_device *out)
{
    return pci_find_class_nth(class_code, 0, out);
}

struct id_query {
    uint16_t vendor;
    const uint16_t *devices;
    unsigned int count;
};

static int match_ids(uint32_t ids, uint32_t class_reg, const void *ctx)
{
    const struct id_query *query = ctx;

    (void)class_reg;
    if ((ids & 0xFFFF) != query->vendor)
        return 0;
    for (unsigned int i = 0; i < query->count; i++)
        if (ids >> 16 == query->devices[i])
            return 1;
    return 0;
}

int pci_find_ids(uint16_t vendor, const uint16_t *devices, unsigned int count, struct pci_device *out)
{
    struct id_query query = {vendor, devices, count};

    return find(match_ids, &query, out);
}

void pci_enable(const struct pci_device *d)
{
    write_config(d->bus, d->dev, d->fn, REG_COMMAND,
                 read_config(d->bus, d->dev, d->fn, REG_COMMAND) | CMD_MEMORY | CMD_BUS_MASTER);
}

uint32_t pci_bar(const struct pci_device *d, unsigned int index)
{
    return read_config(d->bus, d->dev, d->fn, REG_BAR0 + index * 4);
}
