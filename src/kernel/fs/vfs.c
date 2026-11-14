#include "fs/vfs.h"
#include "lib/string.h"
#include "proc/proc.h"

#define MAX_VOLUMES 4
#define MAX_LINK_DEPTH 8
#define VOLUME_NAME_MAX 8

static struct {
    char name[VOLUME_NAME_MAX];
    struct vnode root;
} volumes[MAX_VOLUMES];
static int volume_count;
static int privileged;

void vfs_privileged(int on)
{
    privileged = on;
}

/* Owner bits if the process owns it, else group bits on a gid match, else other. */
int vfs_permit(const struct vnode *node, uint32_t want)
{
    uint32_t uid, gid, bits;

    proc_credentials(&uid, &gid);
    if (privileged || uid == BS_UID_ROOT || !node->owned)
        return 0;
    if (uid == node->uid)
        bits = node->perm >> 6;
    else if (gid == node->gid)
        bits = node->perm >> 3;
    else
        bits = node->perm;
    return (bits & want) == want ? 0 : -BS_EACCES;
}

static void creator(uint32_t *uid, uint32_t *gid)
{
    proc_credentials(uid, gid);
}

int vfs_add_volume(const char *name, const struct vnode *root)
{
    if (volume_count == MAX_VOLUMES ||
        strlcpy_checked(volumes[volume_count].name, name, VOLUME_NAME_MAX) < 0)
        return -BS_EINVAL;
    volumes[volume_count++].root = *root;
    return 0;
}

/* Combines cwd and path into a clean absolute path, resolving . and .. */
static int normalize(const char *cwd, const char *path, char *out)
{
    char joined[BS_PATH_MAX * 2];
    uint32_t used = 0, length = 0;
    const char *p;

    if (path[0] != '/') {
        if (strlcpy_checked(joined, cwd, BS_PATH_MAX) < 0)
            return -BS_ENAMETOOLONG;
        used = strlen(joined);
        joined[used++] = '/';
    }
    if (strlcpy_checked(joined + used, path, BS_PATH_MAX) < 0)
        return -BS_ENAMETOOLONG;

    p = joined;
    out[0] = '/';
    length = 1;
    while (*p) {
        const char *start;
        uint32_t size;

        while (*p == '/')
            p++;
        start = p;
        while (*p && *p != '/')
            p++;
        size = (uint32_t)(p - start);

        if (size == 0 || (size == 1 && start[0] == '.'))
            continue;
        if (size == 2 && start[0] == '.' && start[1] == '.') {
            while (length > 1 && out[--length - 1] != '/')
                ;
            continue;
        }
        if (length + size + 1 >= BS_PATH_MAX)
            return -BS_ENAMETOOLONG;
        memcpy(out + length, start, size);
        length += size;
        out[length++] = '/';
    }
    if (length > 1)
        length--; /* no trailing slash */
    out[length] = 0;
    return 0;
}

static int walk(struct vnode current, const char *path, int follow_last, int depth, struct vnode *out);

static int follow_link(const struct vnode *link, const struct vnode *parent, int depth, struct vnode *out)
{
    char target[BS_PATH_MAX];
    const char *rest = target, *colon = 0;
    struct vnode base = *parent;
    int rc;

    if (depth >= MAX_LINK_DEPTH)
        return -BS_ELOOP;
    if (!link->ops->readlink)
        return -BS_EINVAL;
    rc = link->ops->readlink(link, target, sizeof(target));
    if (rc < 0)
        return rc;

    for (const char *p = target; *p && *p != '/'; p++) {
        if (*p == ':') {
            colon = p;
            break;
        }
    }

    if (colon) {
        uint32_t size = (uint32_t)(colon - target);
        int found = 0;

        for (int i = 0; i < volume_count && !found; i++) {
            if (strlen(volumes[i].name) == size && memcmp(volumes[i].name, target, size) == 0) {
                base = volumes[i].root;
                found = 1;
            }
        }
        if (!found)
            return -BS_ENOENT;
        rest = colon + 1;
    } else if (target[0] == '/') {
        base = volumes[0].root;
    }
    return walk(base, rest, 1, depth + 1, out);
}

static int walk(struct vnode current, const char *path, int follow_last, int depth, struct vnode *out)
{
    char name[BS_NAME_MAX];

    for (;;) {
        uint32_t length = 0;
        struct vnode parent = current;
        int rc;

        while (*path == '/')
            path++;
        if (!*path) {
            *out = current;
            return 0;
        }
        while (*path && *path != '/') {
            if (length + 1 >= BS_NAME_MAX)
                return -BS_ENAMETOOLONG;
            name[length++] = *path++;
        }
        name[length] = 0;

        if (current.type != BS_TYPE_DIR)
            return -BS_ENOTDIR;
        rc = vfs_permit(&current, VFS_EXEC); /* search permission */
        if (rc < 0)
            return rc;
        rc = current.ops->lookup(&parent, name, &current);
        if (rc < 0)
            return rc;

        while (*path == '/')
            path++;
        if (current.type == BS_TYPE_SYMLINK && (*path || follow_last)) {
            rc = follow_link(&current, &parent, depth, &current);
            if (rc < 0)
                return rc;
        }
    }
}

/* Splits an absolute path into its parent directory and final name. */
static int open_parent(const char *cwd, const char *path, struct vnode *dir, char *name)
{
    char clean[BS_PATH_MAX];
    char *slash;
    int rc = normalize(cwd, path, clean);

    if (rc < 0)
        return rc;
    if (clean[1] == 0)
        return -BS_EINVAL;
    slash = clean + strlen(clean);
    while (*--slash != '/')
        ;
    if (strlcpy_checked(name, slash + 1, BS_NAME_MAX) < 0)
        return -BS_ENAMETOOLONG;
    *slash = 0;
    rc = walk(volumes[0].root, clean, 1, 0, dir);
    if (rc < 0)
        return rc;
    return dir->type == BS_TYPE_DIR ? 0 : -BS_ENOTDIR;
}

int vfs_open(const char *cwd, const char *path, struct vnode *out)
{
    char clean[BS_PATH_MAX];
    int rc = normalize(cwd, path, clean);

    if (rc < 0)
        return rc;
    return walk(volumes[0].root, clean, 1, 0, out);
}

int vfs_open_write(const char *cwd, const char *path, struct vnode *out)
{
    struct vnode dir;
    char name[BS_NAME_MAX];
    uint32_t uid, gid;
    int rc = vfs_open(cwd, path, out);

    if (rc == 0) {
        if (out->type == BS_TYPE_DIR)
            return -BS_EISDIR;
        rc = vfs_permit(out, VFS_WRITE);
        if (rc < 0)
            return rc;
        return out->ops->truncate ? out->ops->truncate(out) : -BS_EROFS;
    }
    if (rc != -BS_ENOENT)
        return rc;

    rc = open_parent(cwd, path, &dir, name);
    if (rc == 0)
        rc = vfs_permit(&dir, VFS_WRITE | VFS_EXEC);
    if (rc < 0)
        return rc;
    creator(&uid, &gid);
    return dir.ops->create ? dir.ops->create(&dir, name, BS_TYPE_FILE, 0644, uid, gid, out) : -BS_EROFS;
}

int vfs_mkdir(const char *cwd, const char *path)
{
    struct vnode dir, created;
    char name[BS_NAME_MAX];
    uint32_t uid, gid;
    int rc = open_parent(cwd, path, &dir, name);

    if (rc == 0)
        rc = vfs_permit(&dir, VFS_WRITE | VFS_EXEC);
    if (rc < 0)
        return rc;
    creator(&uid, &gid);
    return dir.ops->create ? dir.ops->create(&dir, name, BS_TYPE_DIR, 0755, uid, gid, &created) : -BS_EROFS;
}

int vfs_unlink(const char *cwd, const char *path)
{
    struct vnode dir;
    char name[BS_NAME_MAX];
    int rc = open_parent(cwd, path, &dir, name);

    if (rc == 0)
        rc = vfs_permit(&dir, VFS_WRITE | VFS_EXEC);
    if (rc < 0)
        return rc;
    return dir.ops->unlink ? dir.ops->unlink(&dir, name) : -BS_EROFS;
}

int vfs_chdir(const char *cwd, const char *path, char *out)
{
    struct vnode node;
    int rc = normalize(cwd, path, out);

    if (rc < 0)
        return rc;
    rc = walk(volumes[0].root, out, 1, 0, &node);
    if (rc < 0)
        return rc;
    if (node.type != BS_TYPE_DIR)
        return -BS_ENOTDIR;
    return vfs_permit(&node, VFS_EXEC);
}

int vfs_stat(const char *cwd, const char *path, struct bs_stat *out)
{
    struct vnode node;
    int rc = vfs_open(cwd, path, &node);

    if (rc < 0)
        return rc;
    out->type = node.type;
    out->size = node.size;
    out->perm = node.owned ? node.perm : node.type == BS_TYPE_DIR ? 0755 : 0644;
    out->uid = node.owned ? node.uid : 0;
    out->gid = node.owned ? node.gid : 0;
    return 0;
}

int vfs_readlink(const char *cwd, const char *path, char *out, uint32_t size)
{
    char clean[BS_PATH_MAX];
    struct vnode node;
    int rc = normalize(cwd, path, clean);

    if (rc == 0)
        rc = walk(volumes[0].root, clean, 0, 0, &node);
    if (rc < 0)
        return rc;
    if (node.type != BS_TYPE_SYMLINK || !node.ops->readlink)
        return -BS_EINVAL;
    return node.ops->readlink(&node, out, size);
}

int vfs_chmod(const char *cwd, const char *path, uint32_t perm)
{
    struct vnode node;
    uint32_t uid, gid;
    int rc = vfs_open(cwd, path, &node);

    if (rc < 0)
        return rc;
    if (!node.owned || !node.ops->setattr)
        return -BS_EROFS;
    proc_credentials(&uid, &gid);
    if (uid != BS_UID_ROOT && uid != node.uid)
        return -BS_EPERM; /* only the owner or root */
    return node.ops->setattr(&node, perm & 0777, node.uid, node.gid);
}

int vfs_chown(const char *cwd, const char *path, uint32_t uid, uint32_t gid)
{
    struct vnode node;
    uint32_t me, my_gid;
    int rc = vfs_open(cwd, path, &node);

    if (rc < 0)
        return rc;
    if (!node.owned || !node.ops->setattr)
        return -BS_EROFS;
    proc_credentials(&me, &my_gid);
    if (me != BS_UID_ROOT)
        return -BS_EPERM;
    return node.ops->setattr(&node, node.perm, uid, gid);
}
