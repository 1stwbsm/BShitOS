#include "lib/lib.h"

void *memset(void *dst, int c, unsigned int n)
{
    UINT8 *d = dst;
    while (n--)
        *d++ = (UINT8)c;
    return dst;
}

void *memcpy(void *dst, const void *src, unsigned int n)
{
    UINT8 *d = dst;
    const UINT8 *s = src;
    while (n--)
        *d++ = *s++;
    return dst;
}

int memcmp(const void *a, const void *b, unsigned int n)
{
    const UINT8 *x = a, *y = b;
    for (; n--; x++, y++)
        if (*x != *y)
            return *x - *y;
    return 0;
}
