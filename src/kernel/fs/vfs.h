/*
 * Virtual filesystem. A backend implements struct fs_ops and registers a named
 * volume; unset write operations make the volume read-only. All paths are
 * normalised here, so backends only ever see single names.
 */
#ifndef KERNEL_VFS_H
#define KERNEL_VFS_H

#include <bshit/syscall.h>

struct vnode;

struct fs_ops {
    int (*lookup)(const struct vnode *dir, const char *name, struct vnode *out);
    int (*read)(const struct vnode *file, uint32_t off, void *buf, uint32_t len);
    /* Returns 1 with an entry, 0 at the end. */
    int (*readdir)(const struct vnode *dir, uint32_t *cursor, struct bs_dirent *out);
    int (*readlink)(const struct vnode *link, char *buf, uint32_t size);
    int (*create)(const struct vnode *dir, const char *name, uint32_t type, struct vnode *out);
    int (*write)(struct vnode *file, uint32_t off, const void *buf, uint32_t len);
    int (*truncate)(struct vnode *file);
    int (*unlink)(const struct vnode *dir, const char *name);
};

struct vnode {
    const struct fs_ops *ops;
    void *fs;
    uint32_t id; /* backend-defined */
    uint32_t type;
    uint32_t size;
};

/* All return 0 or a positive count on success, or a negative BS_E* value. */
int vfs_add_volume(const char *name, const struct vnode *root);
int vfs_open(const char *cwd, const char *path, struct vnode *out);
/* Opens for writing, creating or truncating the file. */
int vfs_open_write(const char *cwd, const char *path, struct vnode *out);
int vfs_mkdir(const char *cwd, const char *path);
int vfs_unlink(const char *cwd, const char *path);
/* Resolves path to a normalised absolute directory path. */
int vfs_chdir(const char *cwd, const char *path, char *out);

#endif
