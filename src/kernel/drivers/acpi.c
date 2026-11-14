#include "arch/x86/cpu.h"
#include "drivers/acpi.h"
#include "drivers/console.h"
#include "lib/string.h"

#define PWRBTN (1u << 8)
#define SCI_EN 1u
#define SLP_EN (1u << 13)
#define SLP_TYP_SHIFT 10
#define HEADER_SIZE 36u
#define AML_PACKAGE 0x12
#define AML_BYTE_PREFIX 0x0A
#define ENABLE_WAIT 300000

#define FADT_DSDT 40
#define FADT_SMI_CMD 48
#define FADT_ACPI_ENABLE 52
#define FADT_PM1A_EVT 56
#define FADT_PM1A_CNT 64
#define FADT_PM1_EVT_LEN 88

struct rsdp {
    char signature[8];
    uint8_t checksum;
    char oem[6];
    uint8_t revision;
    uint32_t rsdt;
    uint32_t length;
    uint64_t xsdt;
} __attribute__((packed));

static uint16_t event_port, enable_port, control_port;
static uint16_t sleep_type;
static int can_power_off;

static inline void outw(uint16_t port, uint16_t value)
{
    __asm__ volatile("outw %w0, %w1" : : "a"(value), "Nd"(port));
}

static inline uint16_t inw(uint16_t port)
{
    uint16_t value;

    __asm__ volatile("inw %w1, %w0" : "=a"(value) : "Nd"(port));
    return value;
}

static uint32_t read32(const uint8_t *p, uint32_t offset)
{
    uint32_t value;

    memcpy(&value, p + offset, 4);
    return value;
}

static const uint8_t *find_table(const struct rsdp *rsdp, const char *signature)
{
    int wide = rsdp->revision >= 2 && rsdp->xsdt != 0 && rsdp->xsdt < 0x100000000ull;
    const uint8_t *root = (const uint8_t *)(uintptr_t)(wide ? (uint32_t)rsdp->xsdt : rsdp->rsdt);
    uint32_t entry_size = wide ? 8 : 4;
    uint32_t count;

    if (!root)
        return 0;
    count = (read32(root, 4) - HEADER_SIZE) / entry_size;
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t *table = (const uint8_t *)(uintptr_t)read32(root, HEADER_SIZE + i * entry_size);

        if (memcmp(table, signature, 4) == 0)
            return table;
    }
    return 0;
}

/* Finds the \_S5 package in the AML and reads its first value. */
static int find_sleep_type(const uint8_t *dsdt, uint16_t *type)
{
    uint32_t length = read32(dsdt, 4);

    for (uint32_t i = HEADER_SIZE; i + 8 < length; i++) {
        const uint8_t *p = dsdt + i;

        if (memcmp(p, "_S5_", 4) != 0 || p[4] != AML_PACKAGE)
            continue;
        p += 5;
        p += ((*p & 0xC0) >> 6) + 2; /* package length, then element count */
        if (*p == AML_BYTE_PREFIX)
            p++;
        *type = *p;
        return 0;
    }
    return -1;
}

void acpi_init(uint64_t rsdp_address)
{
    const struct rsdp *rsdp = (const struct rsdp *)(uintptr_t)rsdp_address;
    const uint8_t *fadt;
    uint32_t smi_command;

    if (!rsdp_address || rsdp_address >= 0x100000000ull)
        return;
    fadt = find_table(rsdp, "FACP");
    if (!fadt)
        return;

    control_port = (uint16_t)read32(fadt, FADT_PM1A_CNT);
    smi_command = read32(fadt, FADT_SMI_CMD);
    if (smi_command && fadt[FADT_ACPI_ENABLE] && !(inw(control_port) & SCI_EN)) {
        outb((uint16_t)smi_command, fadt[FADT_ACPI_ENABLE]);
        for (int i = 0; i < ENABLE_WAIT && !(inw(control_port) & SCI_EN); i++)
            ;
    }

    if (find_sleep_type((const uint8_t *)(uintptr_t)read32(fadt, FADT_DSDT), &sleep_type) == 0)
        can_power_off = 1;

    event_port = (uint16_t)read32(fadt, FADT_PM1A_EVT);
    enable_port = (uint16_t)(event_port + fadt[FADT_PM1_EVT_LEN] / 2);
    if (event_port) {
        outw(event_port, PWRBTN); /* clear anything stale */
        outw(enable_port, inw(enable_port) | PWRBTN);
    }
}

int acpi_power_available(void)
{
    return can_power_off;
}

void acpi_poll(void)
{
    if (event_port && (inw(event_port) & PWRBTN)) {
        outw(event_port, PWRBTN);
        console_graphics_end();
        console_print("\npower button pressed, shutting down\n");
        acpi_poweroff();
    }
}

void acpi_poweroff(void)
{
    if (!can_power_off)
        return;
    outw(control_port, (uint16_t)(sleep_type << SLP_TYP_SHIFT) | SLP_EN);
    for (int i = 0; i < ENABLE_WAIT; i++)
        ;
}
