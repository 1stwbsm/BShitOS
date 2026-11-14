/* CMOS real-time clock, plus resetting the machine through the chipset. */
#include "arch/x86/cpu.h"
#include "drivers/rtc.h"

#define CMOS_INDEX 0x70
#define CMOS_DATA 0x71
#define REG_SECOND 0x00
#define REG_MINUTE 0x02
#define REG_HOUR 0x04
#define REG_DAY 0x07
#define REG_MONTH 0x08
#define REG_YEAR 0x09
#define REG_STATUS_A 0x0A
#define REG_STATUS_B 0x0B
#define STATUS_A_UPDATING 0x80
#define STATUS_B_BINARY 0x04
#define STATUS_B_24H 0x02
#define HOUR_PM 0x80

#define RESET_CONTROL 0xCF9
#define RESET_FULL 0x0E
#define PS2_COMMAND 0x64
#define PS2_PULSE_RESET 0xFE

static uint8_t cmos(uint8_t reg)
{
    outb(CMOS_INDEX, reg);
    return inb(CMOS_DATA);
}

static uint32_t decode(uint8_t value, int binary)
{
    return binary ? value : (uint32_t)(value >> 4) * 10 + (value & 0xF);
}

void rtc_read(struct bs_time *out)
{
    uint8_t status, hour;
    int binary;

    while (cmos(REG_STATUS_A) & STATUS_A_UPDATING)
        ;
    status = cmos(REG_STATUS_B);
    binary = (status & STATUS_B_BINARY) != 0;

    out->second = decode(cmos(REG_SECOND), binary);
    out->minute = decode(cmos(REG_MINUTE), binary);
    hour = cmos(REG_HOUR);
    out->day = decode(cmos(REG_DAY), binary);
    out->month = decode(cmos(REG_MONTH), binary);
    out->year = 2000 + decode(cmos(REG_YEAR), binary);

    out->hour = decode(hour & (uint8_t)~HOUR_PM, binary);
    if (!(status & STATUS_B_24H)) {
        out->hour %= 12;
        if (hour & HOUR_PM)
            out->hour += 12;
    }
}

uint32_t rtc_seconds(void)
{
    struct bs_time t;
    uint32_t y, m, days;

    rtc_read(&t);
    /* Days from civil date (Howard Hinnant's algorithm), relative to 2000-03-01. */
    y = t.year - (t.month <= 2 ? 1 : 0);
    m = t.month <= 2 ? t.month + 9 : t.month - 3;
    days = (y - 2000) * 365 + (y - 2000) / 4 - (y - 2000) / 100 + (y - 2000) / 400 + (153 * m + 2) / 5 + t.day - 1;
    return days * 86400 + t.hour * 3600 + t.minute * 60 + t.second;
}

void reboot_machine(void)
{
    outb(RESET_CONTROL, RESET_FULL);
    outb(PS2_COMMAND, PS2_PULSE_RESET);
    for (;;)
        __asm__ volatile("cli; hlt");
}
