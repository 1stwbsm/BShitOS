/* key=value settings read from the filesystem at boot. */
#ifndef KERNEL_CONF_H
#define KERNEL_CONF_H

#include <stdint.h>

void kconf_load(const char *path);
/* Returns 0 and copies the value, or -1 if the key is absent. */
int kconf_get(const char *key, char *out, unsigned int size);
uint32_t kconf_hex(const char *text);

#endif
