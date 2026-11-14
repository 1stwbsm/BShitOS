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
    int (*create)(const struct vnode *dir, const char *name, uint32_t type, uint32_t perm, uint32_t uid,
                  uint32_t gid, struct vnode *out);
    /* Only on volumes that keep owners. */
    int (*setattr)(struct vnode *node, uint32_t perm, uint32_t uid, uint32_t gid);
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
    int owned; /* the backend keeps owners and rwx bits below */
    uint32_t perm, uid, gid;
};

/* Access wanted, as rwx bits */
#define VFS_READ 4u
#define VFS_WRITE 2u
#define VFS_EXEC 1u

/* All return 0 or a positive count on success, or a negative BS_E* value. */
int vfs_add_volume(const char *name, const struct vnode *root);
int vfs_open(const char *cwd, const char *path, struct vnode *out);
/* Opens for writing, creating or truncating the file. */
int vfs_open_write(const char *cwd, const char *path, struct vnode *out);
int vfs_mkdir(const char *cwd, const char *path);
int vfs_unlink(const char *cwd, const char *path);
/* Resolves path to a normalised absolute directory path. */
int vfs_chdir(const char *cwd, const char *path, char *out);

/* Permissions are checked against the running process; root passes everything. */
int vfs_permit(const struct vnode *node, uint32_t want);
int vfs_stat(const char *cwd, const char *path, struct bs_stat *out);
/* A symlink's target, without following it. */
int vfs_readlink(const char *cwd, const char *path, char *out, uint32_t size);
int vfs_chmod(const char *cwd, const char *path, uint32_t perm);
int vfs_chown(const char *cwd, const char *path, uint32_t uid, uint32_t gid);
/* While set, the kernel acts for itself (reading /conf/shadow) and skips checks. */
void vfs_privileged(int on);

#endif
