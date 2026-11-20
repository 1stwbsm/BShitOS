/* libbs: the BShitOS userspace library (/lib/libbs.blib). */
#ifndef BS_H
#define BS_H

#include <bshit/syscall.h>

void exit(int code) __attribute__((noreturn));
int read(int fd, void *buf, unsigned int n);
int write(int fd, const void *buf, unsigned int n);
int open(const char *path, int flags);
int close(int fd);
int readdir(int fd, struct bs_dirent *out);
int chdir(const char *path);
int getcwd(char *buf, unsigned int size);
int mkdir(const char *path);
int unlink(const char *path);
int spawn(const char *path, int argc, char *const *argv);
void fb_blit(const unsigned int *pixels, unsigned int width, unsigned int height);
int poll_key(void); /* a waiting key, or 0 */
void sleep_ms(unsigned int ms);
void set_serial(int enabled);
void mouse_read(struct bs_mouse *out);
void get_time(struct bs_time *out);
void beep(unsigned int hz, unsigned int ms);
void console_clear(void);
/* Draws the spinning cube into a width x height 0xRRGGBB buffer. */
void cube_render(unsigned int *pixels, int width, int height, int angle_x, int angle_y, int angle_z);
int atoi(const char *s);
void console_size(int *cols, int *rows);
/* Gives the screen back to the text console. */
void graphics_end(void);
void sysinfo(struct bs_sysinfo *out);

/* Users. login() switches this process to the account. A password of 0 means
 * none given, which only root may do; any given password is checked. */
int login(const char *name, const char *password);
int whoami(struct bs_user *out);
int stat(const char *path, struct bs_stat *out);
int chmod(const char *path, unsigned int perm);
int chown(const char *path, unsigned int uid, unsigned int gid);
int set_password(const char *name, const char *old_password, const char *new_password);
int readlink(const char *path, char *out, unsigned int size); /* returns the length */
/* The SATA disk, raw (root only; no writing while the system runs from it). */
int disk_info(struct bs_disk_info *out);
int disk_read(const struct bs_disk_io *io);
int disk_write(const struct bs_disk_io *io);
/* /conf/passwd and /conf/group lookups (users.c). 0 on success. */
int user_by_name(const char *name, struct bs_user *out);
int user_name(unsigned int uid, char *out, unsigned int size);
int group_name(unsigned int gid, char *out, unsigned int size);
int group_by_name(const char *name, unsigned int *gid);
/* name:... tables (/conf/passwd, group, shadow): add a line, or drop name's line. */
int table_append(const char *path, const char *line);
int table_remove(const char *path, const char *name);
/* Reads a line without echoing it. Returns its length, or -1 on Ctrl+C. */
int read_secret(const char *prompt, char *out, unsigned int size);
/* Reads back one row of the text console. Returns its length, or -1. */
int console_line(int row, char *out, unsigned int size);

/* Asks every program to exit, then powers off or restarts (BS_SHUTDOWN_*). It
 * returns: exit soon after. Long-running programs should poll shutting_down(). */
int shutdown(int mode);
int shutting_down(void);
void get_random(void *out, unsigned int length); /* from the kernel */
int cpu_has_cpuid(void); /* a 386 or early 486 has no CPUID */
void net_info(struct bs_netinfo *out);
int dns_lookup(const char *host, unsigned int *ip); /* also accepts a dotted address */
int ping(unsigned int ip, unsigned int timeout_ms); /* returns milliseconds */
int tcp_open(unsigned int ip, unsigned int port);
int tcp_send(int connection, const void *data, unsigned int length);
int tcp_receive(int connection, void *buf, unsigned int size); /* 0 at end of stream */
int tcp_close(int connection);

/* Settings files: "key=value" lines; lines starting with # are comments. */
int conf_get(const char *path, const char *key, char *out, unsigned int size);
int conf_set(const char *path, const char *key, const char *value);

void *memset(void *dst, int c, unsigned int n);
void *memcpy(void *dst, const void *src, unsigned int n);
int memcmp_bytes(const void *a, const void *b, unsigned int n);
unsigned int strlen(const char *s);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, unsigned int n);
char *strchr(const char *s, int c);

/* Supports %s %d %u %x %c %%. */
void printf(const char *format, ...) __attribute__((format(printf, 1, 2)));
const char *strerror(int code); /* accepts positive or negative */

#endif
