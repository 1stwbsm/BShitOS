/* The i8042 PS/2 controller: keyboard and mouse, polled. */
#ifndef KERNEL_PS2_H
#define KERNEL_PS2_H

#include <bshit/syscall.h>

void ps2_init(void);
/* Returns an ASCII or BS_KEY_* code, or 0 if no key is waiting. */
uint8_t ps2_key_poll(void);
/* Returns motion accumulated since the last call. */
void ps2_mouse_read(struct bs_mouse *out);
/* Other input devices (USB) deliver through these. */
void ps2_inject_key(uint8_t key);
void ps2_inject_mouse(int32_t dx, int32_t dy, uint32_t buttons);

#endif
