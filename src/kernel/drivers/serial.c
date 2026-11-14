#include "arch/x86/cpu.h"
#include "drivers/serial.h"

#define COM1 0x3F8
#define LSR_DATA_READY 0x01
#define LSR_TX_EMPTY 0x20

void serial_init(void)
{
    outb(COM1 + 1, 0x00); /* interrupts off */
    outb(COM1 + 3, 0x80); /* divisor latch */
    outb(COM1 + 0, 0x01); /* 115200 baud */
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03); /* 8N1 */
    outb(COM1 + 2, 0xC7); /* FIFOs on */
}

void serial_put(char c)
{
    if (c == '\n')
        serial_put('\r');
    while (!(inb(COM1 + 5) & LSR_TX_EMPTY))
        ;
    outb(COM1, (uint8_t)c);
}

char serial_poll(void)
{
    if (!(inb(COM1 + 5) & LSR_DATA_READY))
        return 0;
    return (char)inb(COM1);
}
