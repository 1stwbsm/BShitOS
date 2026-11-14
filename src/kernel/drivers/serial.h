#ifndef KERNEL_SERIAL_H
#define KERNEL_SERIAL_H

void serial_init(void);
void serial_put(char c);
/* Returns a received byte, or 0 if none is waiting. */
char serial_poll(void);

#endif
