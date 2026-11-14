/* Small string and output helpers libbs does not have. */
#include "bsh.h"

void put(const char *s)
{
    write(1, s, strlen(s));
}

void put_n(const char *s, unsigned int n)
{
    write(1, s, n);
}

void put_padded(const char *s, unsigned int width)
{
    unsigned int length = strlen(s);

    put(s);
    while (length++ < width)
        put(" ");
}

void move_bytes(void *dst, const void *src, unsigned int n)
{
    unsigned char *d = dst;
    const unsigned char *s = src;

    if (d < s)
        while (n--)
            *d++ = *s++;
    else
        while (n--)
            d[n] = s[n];
}

void copy_string(char *dst, const char *src, unsigned int size)
{
    unsigned int length = strlen(src);

    if (size == 0)
        return;
    if (length >= size)
        length = size - 1;
    memcpy(dst, src, length);
    dst[length] = 0;
}

int append(char *dst, unsigned int size, const char *src)
{
    unsigned int used = strlen(dst), length = strlen(src);

    if (used + length >= size)
        return -1;
    memcpy(dst + used, src, length + 1);
    return 0;
}

int starts_with(const char *s, const char *prefix)
{
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

int ends_with(const char *s, const char *suffix)
{
    unsigned int length = strlen(s), suffix_length = strlen(suffix);

    return length >= suffix_length && strcmp(s + length - suffix_length, suffix) == 0;
}

int contains(const char *haystack, const char *needle)
{
    unsigned int n = strlen(needle);

    for (; *haystack; haystack++)
        if (strncmp(haystack, needle, n) == 0)
            return 1;
    return n == 0;
}

void format_uint(unsigned int value, char *out)
{
    char digits[12];
    int n = 0;

    do {
        digits[n++] = (char)('0' + value % 10);
        value /= 10;
    } while (value);
    while (n)
        *out++ = digits[--n];
    *out = 0;
}

int is_name_char(char c, int first)
{
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_')
        return 1;
    return !first && c >= '0' && c <= '9';
}

unsigned int uptime(void)
{
    struct bs_sysinfo info;

    sysinfo(&info);
    return info.uptime_seconds;
}

int bsh_error(const char *what, const char *message)
{
    printf(C_ERROR "bsh: %s: %s" C_RESET "\n", what, message);
    return 1;
}
