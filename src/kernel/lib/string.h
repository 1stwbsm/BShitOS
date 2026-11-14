#ifndef KERNEL_STRING_H
#define KERNEL_STRING_H

#include <stdint.h>

void *memset(void *dst, int c, unsigned int n);
void *memcpy(void *dst, const void *src, unsigned int n);
void *memmove(void *dst, const void *src, unsigned int n);
int memcmp(const void *a, const void *b, unsigned int n);
unsigned int strlen(const char *s);
int strcmp(const char *a, const char *b);
/* Always NUL-terminates; returns -1 if src did not fit. */
int strlcpy_checked(char *dst, const char *src, unsigned int size);

#endif
