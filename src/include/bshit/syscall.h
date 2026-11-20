/* Kernel <-> userspace ABI. Invoked with int 0x80: eax = number, args in ebx, ecx, edx. */
#ifndef BSHIT_SYSCALL_H
#define BSHIT_SYSCALL_H

#include <stdint.h>

enum {
    SYS_EXIT, SYS_READ, SYS_WRITE, SYS_OPEN, SYS_CLOSE, SYS_READDIR,
    SYS_CHDIR, SYS_GETCWD, SYS_MKDIR, SYS_UNLINK, SYS_SPAWN,
    SYS_FB_BLIT, SYS_POLL_KEY, SYS_SLEEP, SYS_SERIAL,
    SYS_MOUSE, SYS_TIME, SYS_BEEP, SYS_CLEAR, SYS_REBOOT, SYS_CONSOLE_SIZE,
    SYS_POWEROFF, SYS_NET_INFO, SYS_DNS, SYS_PING,
    SYS_TCP_OPEN, SYS_TCP_SEND, SYS_TCP_RECEIVE, SYS_TCP_CLOSE,
    SYS_GRAPHICS_END, SYS_SYSINFO, SYS_CONSOLE_LINE,
    SYS_LOGIN, SYS_WHOAMI, SYS_STAT, SYS_CHMOD, SYS_CHOWN, SYS_SETPASS,
    SYS_READLINK, SYS_DISK_INFO, SYS_DISK_READ, SYS_DISK_WRITE,
    SYS_SHUTDOWN, SYS_RANDOM,
};

/* Negative syscall results */
#define BS_EPERM   1
#define BS_ENOENT  2
#define BS_EIO     5
#define BS_ENOEXEC 8
#define BS_EBADF   9
#define BS_EACCES  13
#define BS_ENOMEM  12
#define BS_EFAULT  14
#define BS_EEXIST  17
#define BS_ENOTDIR 20
#define BS_EISDIR  21
#define BS_EINVAL  22
#define BS_EMFILE  24
#define BS_ENOSPC  28
#define BS_EROFS   30
#define BS_ENAMETOOLONG 36
#define BS_ENOTEMPTY 39
#define BS_ELOOP   40
#define BS_ENETDOWN 100
#define BS_EHOSTUNREACH 101
#define BS_ETIMEDOUT 110
#define BS_ECONNREFUSED 111

#define BS_O_READ  0
#define BS_O_WRITE 1 /* create if missing, truncate if present */

#define BS_PATH_MAX 256
#define BS_NAME_MAX 60

#define BS_TYPE_FILE 1
#define BS_TYPE_DIR 2
#define BS_TYPE_SYMLINK 3

struct bs_dirent {
    char name[BS_NAME_MAX];
    uint32_t type;
    uint32_t size;
};

/* Non-ASCII key codes from poll_key */
#define BS_KEY_UP    0x80
#define BS_KEY_DOWN  0x81
#define BS_KEY_LEFT  0x82
#define BS_KEY_RIGHT 0x83
#define BS_KEY_HOME  0x84
#define BS_KEY_END   0x85
#define BS_KEY_DEL   0x86
#define BS_KEY_PGUP  0x87
#define BS_KEY_PGDN  0x88

#define BS_MOUSE_LEFT  1u
#define BS_MOUSE_RIGHT 2u

struct bs_mouse {
    int32_t dx, dy; /* motion since the last read; +y is down */
    uint32_t buttons;
};

struct bs_netinfo {
    uint32_t present;
    uint32_t ip, mask, gateway, dns; /* host byte order */
    uint8_t mac[6];
    uint8_t padding[2];
};

struct bs_sysinfo {
    uint32_t memory_total_kib, memory_free_kib; /* memory for programs */
    uint32_t disk_total_kib, disk_free_kib;     /* the root filesystem */
    uint32_t uptime_seconds;
    uint32_t screen_width, screen_height;
    uint32_t flags; /* BS_SYSINFO_* */
};

#define BS_SYSINFO_LIVE 1u /* booted from the live image: nothing is saved */
#define BS_SYSINFO_SHUTDOWN 2u /* a shutdown is under way: finish up and exit */

/* SYS_SHUTDOWN modes. Every program is asked to exit first; see proc/shutdown.c. */
#define BS_SHUTDOWN_POWEROFF 1
#define BS_SHUTDOWN_REBOOT 2
/* The exit code of a program stopped for not exiting before a shutdown deadline. */
#define BS_EXIT_STOPPED 143

/* Who the calling process runs as (SYS_WHOAMI). */
struct bs_user {
    uint32_t uid, gid;
    char name[32];
    char home[BS_PATH_MAX];
    char shell[BS_PATH_MAX];
};

/* SYS_STAT; perm is 0755 style. Volumes without owners report root and 0755/0644. */
struct bs_stat {
    uint32_t type, size, perm, uid, gid;
};

/* The SATA disk, for the installer. Raw access is root only, and writing is
 * refused while the running system's root filesystem lives on it. */
struct bs_disk_info {
    uint32_t present, in_use;
    uint32_t sectors_low, sectors_high; /* 512-byte sectors */
    char model[44];
};

struct bs_disk_io {
    uint32_t lba_low, lba_high;
    uint32_t sectors; /* at most BS_DISK_IO_MAX per call */
    uint32_t buffer;
};

#define BS_DISK_SECTOR 512u
#define BS_DISK_IO_MAX 128u

#define BS_UID_ROOT 0u
#define BS_USER_NAME_MAX 32u

struct bs_time {
    uint32_t year, month, day, hour, minute, second; /* UTC */
};

/* Process memory layout */
#define BS_USER_BASE 0x40000000u
#define BS_LIB_BASE  0x40400000u
#define BS_USER_TOP  0x40800000u

#endif
