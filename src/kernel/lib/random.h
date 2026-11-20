#ifndef KERNEL_RANDOM_H
#define KERNEL_RANDOM_H

#include <stdint.h>

void random_bytes(void *out, uint32_t length);

#endif
