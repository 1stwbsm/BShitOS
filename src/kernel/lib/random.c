/*
 * Randomness for salts, GUIDs and TLS. Timing jitter (the cycle counter where
 * the CPU has one, else the PIT's counter), the clock and a running state,
 * through SHA-256. Not a hardware RNG, but unpredictable enough for these.
 */
#include <bshit/passhash.h>

#include "arch/x86/cpu.h"
#include "arch/x86/cpuinfo.h"
#include "drivers/rtc.h"
#include "lib/random.h"
#include "lib/string.h"

#define PIT_COMMAND 0x43
#define PIT_CHANNEL0 0x40

static uint8_t state[32];
static uint32_t counter;

static uint32_t jitter(void)
{
    uint32_t low = 0, high = 0;

    if (cpu.has_tsc) {
        __asm__ volatile("rdtsc" : "=a"(low), "=d"(high));
        return low ^ high;
    }
    outb(PIT_COMMAND, 0x00); /* latch channel 0 */
    low = inb(PIT_CHANNEL0);
    high = inb(PIT_CHANNEL0);
    return low | high << 8;
}

void random_bytes(void *out, uint32_t length)
{
    uint8_t *p = out, block[32];
    struct pass_sha256 s;
    uint32_t seconds = rtc_seconds();

    pass_sha256_init(&s);
    pass_sha256_update(&s, state, sizeof(state));
    pass_sha256_update(&s, &seconds, sizeof(seconds));
    for (int i = 0; i < 64; i++) {
        uint32_t t = jitter();

        pass_sha256_update(&s, &t, sizeof(t));
        for (volatile int spin = 0; spin < (int)(t & 63); spin++) /* uneven gaps between samples */
            ;
    }
    pass_sha256_final(&s, state);

    while (length) {
        uint32_t n = length < sizeof(block) ? length : sizeof(block);

        counter++;
        pass_sha256_init(&s);
        pass_sha256_update(&s, state, sizeof(state));
        pass_sha256_update(&s, &counter, sizeof(counter));
        pass_sha256_final(&s, block);
        memcpy(p, block, n);
        p += n;
        length -= n;
    }
}
