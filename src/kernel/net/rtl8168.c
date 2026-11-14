/* Realtek RTL8168/8111 PCIe Gigabit Ethernet, polled. */
#include "drivers/pci.h"
#include "drivers/pit.h"
#include "lib/string.h"
#include "net/nic.h"

#define VENDOR_REALTEK 0x10EC
#define MMIO_BAR 2

#define REG_MAC 0x00
#define REG_TNPDS 0x20
#define REG_CR 0x37
#define REG_TPPOLL 0x38
#define REG_IMR 0x3C
#define REG_ISR 0x3E
#define REG_TCR 0x40
#define REG_RCR 0x44
#define REG_9346CR 0x50
#define REG_RMS 0xDA
#define REG_CPLUS 0xE0
#define REG_RDSAR 0xE4
#define REG_MTPS 0xEC

#define CR_RESET 0x10
#define CR_ENABLE 0x0C
#define UNLOCK 0xC0
#define LOCK 0x00
#define TPPOLL_NORMAL 0x40
#define TCR_VALUE 0x03000700u  /* standard gap, unlimited DMA burst */
#define RCR_VALUE 0x0000E70Eu  /* broadcast, multicast, own MAC */
#define CPLUS_RX_CHECKSUM 0x0020
#define MTPS_VALUE 0x3B
#define RESET_WAIT_MS 100

#define OWN (1u << 31)
#define END_OF_RING (1u << 30)
#define FIRST (1u << 29)
#define LAST (1u << 28)
#define LENGTH_MASK 0x3FFFu
#define CRC_BYTES 4u
#define MIN_FRAME 60u

#define RX_COUNT 32u
#define TX_COUNT 16u
#define BUFFER_SIZE 2048u
#define TX_WAIT_POLLS 1000000

struct descriptor {
    uint32_t options, vlan, address_low, address_high;
};

static volatile struct descriptor rx_ring[RX_COUNT] __attribute__((aligned(256)));
static volatile struct descriptor tx_ring[TX_COUNT] __attribute__((aligned(256)));
static uint8_t rx_buffers[RX_COUNT][BUFFER_SIZE];
static uint8_t tx_buffers[TX_COUNT][BUFFER_SIZE];
static uint32_t rx_next, tx_next;
static volatile uint8_t *regs;
static uint8_t mac_address[6];

static void wr8(uint32_t reg, uint8_t value) { *(volatile uint8_t *)(regs + reg) = value; }
static void wr16(uint32_t reg, uint16_t value) { *(volatile uint16_t *)(regs + reg) = value; }
static void wr32(uint32_t reg, uint32_t value) { *(volatile uint32_t *)(regs + reg) = value; }
static uint8_t rd8(uint32_t reg) { return *(volatile uint8_t *)(regs + reg); }
static uint16_t rd16(uint32_t reg) { return *(volatile uint16_t *)(regs + reg); }

static uint32_t rx_options(uint32_t index)
{
    return OWN | (index == RX_COUNT - 1 ? END_OF_RING : 0) | BUFFER_SIZE;
}

static int rtl8168_init(void)
{
    static const uint16_t supported[] = {0x8168, 0x8161, 0x8136};
    struct pci_device device;
    int waited = 0;

    if (pci_find_ids(VENDOR_REALTEK, supported, sizeof(supported) / sizeof(supported[0]), &device) < 0)
        return -1;
    pci_enable(&device);
    regs = (volatile uint8_t *)(pci_bar(&device, MMIO_BAR) & ~0xFu);

    wr8(REG_CR, CR_RESET);
    while ((rd8(REG_CR) & CR_RESET) && waited++ < RESET_WAIT_MS)
        pit_sleep_ms(1);
    if (rd8(REG_CR) & CR_RESET)
        return -1;

    for (uint32_t i = 0; i < 6; i++)
        mac_address[i] = rd8(REG_MAC + i);

    wr8(REG_9346CR, UNLOCK);
    wr16(REG_IMR, 0);
    wr16(REG_ISR, 0xFFFF);
    wr16(REG_CPLUS, rd16(REG_CPLUS) | CPLUS_RX_CHECKSUM);
    wr16(REG_RMS, BUFFER_SIZE);
    wr8(REG_MTPS, MTPS_VALUE);

    for (uint32_t i = 0; i < RX_COUNT; i++) {
        rx_ring[i].vlan = 0;
        rx_ring[i].address_low = (uint32_t)rx_buffers[i];
        rx_ring[i].address_high = 0;
        rx_ring[i].options = rx_options(i);
    }
    for (uint32_t i = 0; i < TX_COUNT; i++) {
        tx_ring[i].vlan = 0;
        tx_ring[i].address_low = (uint32_t)tx_buffers[i];
        tx_ring[i].address_high = 0;
        tx_ring[i].options = i == TX_COUNT - 1 ? END_OF_RING : 0;
    }
    wr32(REG_RDSAR, (uint32_t)rx_ring);
    wr32(REG_RDSAR + 4, 0);
    wr32(REG_TNPDS, (uint32_t)tx_ring);
    wr32(REG_TNPDS + 4, 0);

    wr8(REG_CR, CR_ENABLE);
    wr32(REG_TCR, TCR_VALUE);
    wr32(REG_RCR, RCR_VALUE);
    wr8(REG_9346CR, LOCK);
    return 0;
}

static void rtl8168_mac(uint8_t out[6])
{
    memcpy(out, mac_address, 6);
}

static void rtl8168_send(const void *frame, uint32_t length)
{
    volatile struct descriptor *slot = &tx_ring[tx_next];
    uint32_t end = tx_next == TX_COUNT - 1 ? END_OF_RING : 0;

    if (length > BUFFER_SIZE)
        return;
    for (int i = 0; i < TX_WAIT_POLLS && (slot->options & OWN); i++)
        ;
    memcpy(tx_buffers[tx_next], frame, length);
    if (length < MIN_FRAME) {
        memset(tx_buffers[tx_next] + length, 0, MIN_FRAME - length);
        length = MIN_FRAME;
    }
    __asm__ volatile("" ::: "memory");
    slot->options = OWN | FIRST | LAST | end | length;
    tx_next = (tx_next + 1) % TX_COUNT;
    wr8(REG_TPPOLL, TPPOLL_NORMAL);
}

static uint32_t rtl8168_receive(void *frame)
{
    volatile struct descriptor *slot = &rx_ring[rx_next];
    uint32_t options = slot->options, length;

    if (options & OWN)
        return 0;
    length = (options & LENGTH_MASK);
    length = length > CRC_BYTES && length <= BUFFER_SIZE ? length - CRC_BYTES : 0;
    if (length)
        memcpy(frame, rx_buffers[rx_next], length);
    __asm__ volatile("" ::: "memory");
    slot->options = rx_options(rx_next);
    rx_next = (rx_next + 1) % RX_COUNT;
    return length;
}

const struct nic_driver rtl8168_driver = {
    .name = "Realtek RTL8168",
    .init = rtl8168_init,
    .mac = rtl8168_mac,
    .send = rtl8168_send,
    .receive = rtl8168_receive,
};
