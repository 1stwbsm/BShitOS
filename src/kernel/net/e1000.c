/* Intel e1000-family Ethernet, using legacy descriptors, polled. */
#include "drivers/pci.h"
#include "drivers/pit.h"
#include "lib/string.h"
#include "net/net.h"
#include "net/nic.h"

#define VENDOR_INTEL 0x8086

#define REG_CTRL 0x0000
#define REG_IMC 0x00D8
#define REG_RCTL 0x0100
#define REG_TCTL 0x0400
#define REG_TIPG 0x0410
#define REG_RDBAL 0x2800
#define REG_RDBAH 0x2804
#define REG_RDLEN 0x2808
#define REG_RDH 0x2810
#define REG_RDT 0x2818
#define REG_TDBAL 0x3800
#define REG_TDBAH 0x3804
#define REG_TDLEN 0x3808
#define REG_TDH 0x3810
#define REG_TDT 0x3818
#define REG_MTA 0x5200
#define REG_RAL 0x5400
#define REG_RAH 0x5404

#define CTRL_SLU (1u << 6)
#define CTRL_RST (1u << 26)
#define RCTL_EN (1u << 1)
#define RCTL_BAM (1u << 15)
#define RCTL_SECRC (1u << 26)
#define TCTL_EN (1u << 1)
#define TCTL_PSP (1u << 3)
#define TCTL_CT (0x10u << 4)
#define TCTL_COLD (0x40u << 12)
#define TIPG_VALUE 0x0060200Au
#define MTA_ENTRIES 128

#define RX_COUNT 32u
#define TX_COUNT 16u
#define BUFFER_SIZE 2048u
#define STATUS_DONE 0x01
#define TX_EOP 0x01
#define TX_IFCS 0x02
#define TX_RS 0x08
#define TX_WAIT_POLLS 1000000

struct rx_descriptor {
    uint64_t address;
    uint16_t length, checksum;
    uint8_t status, errors;
    uint16_t special;
};

struct tx_descriptor {
    uint64_t address;
    uint16_t length;
    uint8_t cso, command, status, css;
    uint16_t special;
};

static volatile struct rx_descriptor rx_ring[RX_COUNT] __attribute__((aligned(16)));
static volatile struct tx_descriptor tx_ring[TX_COUNT] __attribute__((aligned(16)));
static uint8_t rx_buffers[RX_COUNT][BUFFER_SIZE];
static uint8_t tx_buffers[TX_COUNT][BUFFER_SIZE];
static uint32_t rx_next, tx_next;
static volatile uint8_t *regs;
static uint8_t mac[6];

static uint32_t rd(uint32_t reg)
{
    return *(volatile uint32_t *)(regs + reg);
}

static void wr(uint32_t reg, uint32_t value)
{
    *(volatile uint32_t *)(regs + reg) = value;
}

static int e1000_init(void)
{
    static const uint16_t supported[] = {0x100E, 0x100F, 0x1004, 0x10D3};
    struct pci_device device;
    uint32_t low, high;

    if (pci_find_ids(VENDOR_INTEL, supported, sizeof(supported) / sizeof(supported[0]), &device) < 0)
        return -1;
    pci_enable(&device);
    regs = (volatile uint8_t *)(pci_bar(&device, 0) & ~0xFu);

    wr(REG_IMC, 0xFFFFFFFFu);
    wr(REG_CTRL, rd(REG_CTRL) | CTRL_RST);
    pit_sleep_ms(10);
    wr(REG_IMC, 0xFFFFFFFFu);
    wr(REG_CTRL, rd(REG_CTRL) | CTRL_SLU);

    low = rd(REG_RAL);
    high = rd(REG_RAH);
    mac[0] = (uint8_t)low;
    mac[1] = (uint8_t)(low >> 8);
    mac[2] = (uint8_t)(low >> 16);
    mac[3] = (uint8_t)(low >> 24);
    mac[4] = (uint8_t)high;
    mac[5] = (uint8_t)(high >> 8);

    for (uint32_t i = 0; i < MTA_ENTRIES; i++)
        wr(REG_MTA + i * 4, 0);

    for (uint32_t i = 0; i < RX_COUNT; i++) {
        rx_ring[i].address = (uint32_t)rx_buffers[i];
        rx_ring[i].status = 0;
    }
    wr(REG_RDBAL, (uint32_t)rx_ring);
    wr(REG_RDBAH, 0);
    wr(REG_RDLEN, sizeof(rx_ring));
    wr(REG_RDH, 0);
    wr(REG_RDT, RX_COUNT - 1);
    wr(REG_RCTL, RCTL_EN | RCTL_BAM | RCTL_SECRC);

    for (uint32_t i = 0; i < TX_COUNT; i++) {
        tx_ring[i].address = (uint32_t)tx_buffers[i];
        tx_ring[i].status = STATUS_DONE;
    }
    wr(REG_TDBAL, (uint32_t)tx_ring);
    wr(REG_TDBAH, 0);
    wr(REG_TDLEN, sizeof(tx_ring));
    wr(REG_TDH, 0);
    wr(REG_TDT, 0);
    wr(REG_TIPG, TIPG_VALUE);
    wr(REG_TCTL, TCTL_EN | TCTL_PSP | TCTL_CT | TCTL_COLD);
    return 0;
}

static void e1000_mac(uint8_t out[6])
{
    memcpy(out, mac, 6);
}

static void e1000_send(const void *frame, uint32_t length)
{
    volatile struct tx_descriptor *slot = &tx_ring[tx_next];

    if (!regs || length > BUFFER_SIZE)
        return;
    for (int i = 0; i < TX_WAIT_POLLS && !(slot->status & STATUS_DONE); i++)
        ;

    memcpy(tx_buffers[tx_next], frame, length);
    slot->length = (uint16_t)length;
    slot->command = TX_EOP | TX_IFCS | TX_RS;
    slot->status = 0;
    __asm__ volatile("" ::: "memory");
    tx_next = (tx_next + 1) % TX_COUNT;
    wr(REG_TDT, tx_next);
}

static uint32_t e1000_receive(void *frame)
{
    volatile struct rx_descriptor *slot = &rx_ring[rx_next];
    uint32_t length;

    if (!regs || !(slot->status & STATUS_DONE))
        return 0;
    length = slot->length;
    if (length > BUFFER_SIZE)
        length = 0;
    memcpy(frame, rx_buffers[rx_next], length);
    slot->status = 0;
    __asm__ volatile("" ::: "memory");
    wr(REG_RDT, rx_next);
    rx_next = (rx_next + 1) % RX_COUNT;
    return length;
}

const struct nic_driver e1000_driver = {
    .name = "Intel e1000",
    .init = e1000_init,
    .mac = e1000_mac,
    .send = e1000_send,
    .receive = e1000_receive,
};
