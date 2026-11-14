#include <stdarg.h>

#include "bs.h"

static inline int syscall3(int number, int a, int b, int c)
{
    int result;

    __asm__ volatile("int $0x80" : "=a"(result) : "a"(number), "b"(a), "c"(b), "d"(c) : "memory");
    return result;
}

void exit(int code)
{
    syscall3(SYS_EXIT, code, 0, 0);
    for (;;)
        ;
}

int read(int fd, void *buf, unsigned int n) { return syscall3(SYS_READ, fd, (int)buf, (int)n); }
int write(int fd, const void *buf, unsigned int n) { return syscall3(SYS_WRITE, fd, (int)buf, (int)n); }
int open(const char *path, int flags) { return syscall3(SYS_OPEN, (int)path, flags, 0); }
int close(int fd) { return syscall3(SYS_CLOSE, fd, 0, 0); }
int readdir(int fd, struct bs_dirent *out) { return syscall3(SYS_READDIR, fd, (int)out, 0); }
int chdir(const char *path) { return syscall3(SYS_CHDIR, (int)path, 0, 0); }
int getcwd(char *buf, unsigned int size) { return syscall3(SYS_GETCWD, (int)buf, (int)size, 0); }
int mkdir(const char *path) { return syscall3(SYS_MKDIR, (int)path, 0, 0); }
int unlink(const char *path) { return syscall3(SYS_UNLINK, (int)path, 0, 0); }
int spawn(const char *path, int argc, char *const *argv) { return syscall3(SYS_SPAWN, (int)path, argc, (int)argv); }
void fb_blit(const unsigned int *pixels, unsigned int width, unsigned int height) { syscall3(SYS_FB_BLIT, (int)pixels, (int)width, (int)height); }
int poll_key(void) { return syscall3(SYS_POLL_KEY, 0, 0, 0); }
void sleep_ms(unsigned int ms) { syscall3(SYS_SLEEP, (int)ms, 0, 0); }
void set_serial(int enabled) { syscall3(SYS_SERIAL, enabled, 0, 0); }
void mouse_read(struct bs_mouse *out) { syscall3(SYS_MOUSE, (int)out, 0, 0); }
void get_time(struct bs_time *out) { syscall3(SYS_TIME, (int)out, 0, 0); }
void beep(unsigned int hz, unsigned int ms) { syscall3(SYS_BEEP, (int)hz, (int)ms, 0); }
void console_clear(void) { syscall3(SYS_CLEAR, 0, 0, 0); }
void graphics_end(void) { syscall3(SYS_GRAPHICS_END, 0, 0, 0); }
void sysinfo(struct bs_sysinfo *out) { syscall3(SYS_SYSINFO, (int)out, 0, 0); }
int login(const char *name, const char *password) { return syscall3(SYS_LOGIN, (int)name, (int)password, 0); }
int whoami(struct bs_user *out) { return syscall3(SYS_WHOAMI, (int)out, 0, 0); }
int stat(const char *path, struct bs_stat *out) { return syscall3(SYS_STAT, (int)path, (int)out, 0); }
int chmod(const char *path, unsigned int perm) { return syscall3(SYS_CHMOD, (int)path, (int)perm, 0); }
int chown(const char *path, unsigned int uid, unsigned int gid) { return syscall3(SYS_CHOWN, (int)path, (int)uid, (int)gid); }
int set_password(const char *name, const char *old_password, const char *new_password) { return syscall3(SYS_SETPASS, (int)name, (int)old_password, (int)new_password); }
int readlink(const char *path, char *out, unsigned int size) { return syscall3(SYS_READLINK, (int)path, (int)out, (int)size); }
int disk_info(struct bs_disk_info *out) { return syscall3(SYS_DISK_INFO, (int)out, 0, 0); }
int disk_read(const struct bs_disk_io *io) { return syscall3(SYS_DISK_READ, (int)io, 0, 0); }
int disk_write(const struct bs_disk_io *io) { return syscall3(SYS_DISK_WRITE, (int)io, 0, 0); }
int console_line(int row, char *out, unsigned int size) { return syscall3(SYS_CONSOLE_LINE, row, (int)out, (int)size); }
void poweroff(void) { syscall3(SYS_POWEROFF, 0, 0, 0); }
void net_info(struct bs_netinfo *out) { syscall3(SYS_NET_INFO, (int)out, 0, 0); }
int ping(unsigned int ip, unsigned int timeout_ms) { return syscall3(SYS_PING, (int)ip, (int)timeout_ms, 0); }
int tcp_open(unsigned int ip, unsigned int port) { return syscall3(SYS_TCP_OPEN, (int)ip, (int)port, 0); }
int tcp_send(int connection, const void *data, unsigned int length) { return syscall3(SYS_TCP_SEND, connection, (int)data, (int)length); }
int tcp_receive(int connection, void *buf, unsigned int size) { return syscall3(SYS_TCP_RECEIVE, connection, (int)buf, (int)size); }
int tcp_close(int connection) { return syscall3(SYS_TCP_CLOSE, connection, 0, 0); }

int dns_lookup(const char *host, unsigned int *ip)
{
    unsigned int value = 0, part = 0, parts = 0;
    const char *p = host;

    for (;; p++) {
        if (*p >= '0' && *p <= '9') {
            part = part * 10 + (unsigned int)(*p - '0');
        } else if (*p == '.' || !*p) {
            value = value << 8 | part;
            part = 0;
            parts++;
            if (!*p)
                break;
        } else {
            return syscall3(SYS_DNS, (int)host, (int)ip, 0);
        }
    }
    if (parts != 4)
        return syscall3(SYS_DNS, (int)host, (int)ip, 0);
    *ip = value;
    return 0;
}

void console_size(int *cols, int *rows)
{
    int packed = syscall3(SYS_CONSOLE_SIZE, 0, 0, 0);

    *cols = packed >> 16;
    *rows = packed & 0xFFFF;
}

void reboot(void)
{
    syscall3(SYS_REBOOT, 0, 0, 0);
    for (;;)
        ;
}

int atoi(const char *s)
{
    int value = 0;

    while (*s >= '0' && *s <= '9')
        value = value * 10 + (*s++ - '0');
    return value;
}

void *memset(void *dst, int c, unsigned int n)
{
    unsigned char *d = dst;
    while (n--)
        *d++ = (unsigned char)c;
    return dst;
}

void *memcpy(void *dst, const void *src, unsigned int n)
{
    unsigned char *d = dst;
    const unsigned char *s = src;
    while (n--)
        *d++ = *s++;
    return dst;
}

int memcmp_bytes(const void *a, const void *b, unsigned int n)
{
    const unsigned char *x = a, *y = b;

    for (; n; n--, x++, y++)
        if (*x != *y)
            return *x - *y;
    return 0;
}

unsigned int strlen(const char *s)
{
    unsigned int n = 0;
    while (s[n])
        n++;
    return n;
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, unsigned int n)
{
    for (; n; n--, a++, b++) {
        if (*a != *b || !*a)
            return (unsigned char)*a - (unsigned char)*b;
    }
    return 0;
}

char *strchr(const char *s, int c)
{
    for (;; s++) {
        if (*s == (char)c)
            return (char *)s;
        if (!*s)
            return 0;
    }
}

static void print_number(unsigned int value, unsigned int base)
{
    char buf[12];
    int i = 12;

    do {
        buf[--i] = "0123456789abcdef"[value % base];
        value /= base;
    } while (value);
    write(1, buf + i, (unsigned int)(12 - i));
}

void printf(const char *format, ...)
{
    va_list args;

    va_start(args, format);
    while (*format) {
        const char *run = format;

        while (*format && *format != '%')
            format++;
        if (format != run)
            write(1, run, (unsigned int)(format - run));
        if (!*format)
            break;

        format++;
        switch (*format) {
        case 's': {
            const char *s = va_arg(args, const char *);
            write(1, s, strlen(s));
            break;
        }
        case 'd': {
            int v = va_arg(args, int);
            if (v < 0) {
                write(1, "-", 1);
                v = -v;
            }
            print_number((unsigned int)v, 10);
            break;
        }
        case 'u':
            print_number(va_arg(args, unsigned int), 10);
            break;
        case 'x':
            print_number(va_arg(args, unsigned int), 16);
            break;
        case 'c': {
            char c = (char)va_arg(args, int);
            write(1, &c, 1);
            break;
        }
        case '%':
            write(1, "%", 1);
            break;
        default:
            continue; /* unknown: re-examine it as literal text */
        }
        format++;
    }
    va_end(args);
}

const char *strerror(int code)
{
    switch (code < 0 ? -code : code) {
    case BS_EPERM:   return "not allowed (root only)";
    case BS_ENOENT:  return "no such file or directory";
    case BS_EIO:     return "disk error";
    case BS_ENOEXEC: return "not a valid .BS program";
    case BS_EBADF:   return "bad file descriptor";
    case BS_ENOMEM:  return "out of memory";
    case BS_EACCES:  return "permission denied";
    case BS_EFAULT:  return "bad address";
    case BS_EEXIST:  return "already exists";
    case BS_ENOTDIR: return "not a directory";
    case BS_EISDIR:  return "is a directory";
    case BS_EINVAL:  return "invalid argument";
    case BS_EMFILE:  return "too many open files";
    case BS_ENOSPC:  return "no space left";
    case BS_EROFS:   return "read-only filesystem";
    case BS_ENAMETOOLONG: return "name too long";
    case BS_ENOTEMPTY:    return "directory not empty";
    case BS_ELOOP:   return "too many symlinks";
    case BS_ENETDOWN: return "the network is down";
    case BS_EHOSTUNREACH: return "no route to host";
    case BS_ETIMEDOUT: return "timed out";
    case BS_ECONNREFUSED: return "connection refused";
    default:         return "unknown error";
    }
}
