#ifndef KERNEL_PROC_H
#define KERNEL_PROC_H

#include <bshit/bootinfo.h>
#include <bshit/syscall.h>

#include "fs/vfs.h"

#define PROC_MAX_FILES 16
#define FD_FIRST_FILE 3 /* 0-2 are the console */

struct file {
    int used;
    int writable;
    struct vnode node;
    uint32_t offset;
};

struct proc {
    int used;
    uint32_t frames[2];
    uint32_t ksp;
    char cwd[BS_PATH_MAX];
    struct file files[PROC_MAX_FILES];
    struct proc *parent;
    struct bs_user user; /* who it runs as; children inherit it */
};

void proc_init(const struct bootinfo *info);
struct proc *proc_current(void);
/* Runs a program to completion; returns its exit code or a negative BS_E*. */
int proc_run(const char *path, int argc, char *const *argv);
void proc_exit(int code) __attribute__((noreturn));
void proc_memory(uint32_t *total_kib, uint32_t *free_kib);
/* The running process's uid and gid; root while the kernel runs for itself. */
void proc_credentials(uint32_t *uid, uint32_t *gid);

int syscall_dispatch(uint32_t number, uint32_t a, uint32_t b, uint32_t c);

#endif
