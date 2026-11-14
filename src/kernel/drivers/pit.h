#ifndef KERNEL_PIT_H
#define KERNEL_PIT_H

#include <stdint.h>

/* Busy-waits using PIT channel 0, so it needs no interrupts. */
void pit_sleep_ms(uint32_t ms);
/* Plays a square wave on the PC speaker, blocking for its duration. */
void pit_beep(uint32_t hz, uint32_t ms);

#endif
