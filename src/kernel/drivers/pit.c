#include "arch/x86/cpu.h"
#include "drivers/pit.h"

#define PIT_HZ 1193182u
#define PIT_CHANNEL0 0x40
#define PIT_CHANNEL2 0x42
#define PIT_COMMAND 0x43
#define PIT_ONESHOT_CH0 0x30 /* channel 0, lobyte/hibyte, mode 0 */
#define PIT_SQUARE_CH2 0xB6  /* channel 2, lobyte/hibyte, mode 3 */
#define PIT_READBACK_STATUS_CH0 0xE2
#define PIT_STATUS_OUTPUT 0x80
#define SPEAKER_PORT 0x61
#define SPEAKER_ON 0x03
#define MAX_CHUNK_MS 50u
#define MIN_HZ 20u
#define MAX_HZ 20000u

static void wait_chunk(uint32_t ms)
{
    uint32_t count = PIT_HZ / 1000 * ms;

    outb(PIT_COMMAND, PIT_ONESHOT_CH0);
    outb(PIT_CHANNEL0, (uint8_t)count);
    outb(PIT_CHANNEL0, (uint8_t)(count >> 8));
    for (;;) {
        outb(PIT_COMMAND, PIT_READBACK_STATUS_CH0);
        if (inb(PIT_CHANNEL0) & PIT_STATUS_OUTPUT)
            return;
    }
}

void pit_sleep_ms(uint32_t ms)
{
    while (ms > 0) {
        uint32_t chunk = ms < MAX_CHUNK_MS ? ms : MAX_CHUNK_MS;

        wait_chunk(chunk);
        ms -= chunk;
    }
}

void pit_beep(uint32_t hz, uint32_t ms)
{
    uint32_t divisor;

    if (hz < MIN_HZ || hz > MAX_HZ) {
        pit_sleep_ms(ms);
        return;
    }
    divisor = PIT_HZ / hz;
    outb(PIT_COMMAND, PIT_SQUARE_CH2);
    outb(PIT_CHANNEL2, (uint8_t)divisor);
    outb(PIT_CHANNEL2, (uint8_t)(divisor >> 8));
    outb(SPEAKER_PORT, inb(SPEAKER_PORT) | SPEAKER_ON);
    pit_sleep_ms(ms);
    outb(SPEAKER_PORT, inb(SPEAKER_PORT) & (uint8_t)~SPEAKER_ON);
}
