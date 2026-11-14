#ifndef KERNEL_KEYBOARD_H
#define KERNEL_KEYBOARD_H

void keyboard_init(void);
/* Returns an ASCII key from the PS/2 keyboard, or 0 if none is waiting. */
char keyboard_poll(void);

#endif
