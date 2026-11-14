/*
 * Formats BSFS on the new partition and copies the live system into it.
 * Blocks and inodes are handed out in order, so every file is one contiguous
 * run and the bitmaps are "first N used". Layout as in docs/BSFS.md.
 */
#include <bshit/bsfs.h>

#include "install.h"

#define MAX_INODES 65536u
#define MIN_INODES 4096u
#define CACHED_ITABLE_BLOCKS 64u /* the first 2048 inodes, which is all a copy uses */
#define BATCH_BLOCKS 16u
#define ARENA_BYTES 65536u
#define HOMES "userspace"

static struct {
    u64 lba;
    unsigned int total, inode_count;
    unsigned int ibitmap_start, ibitmap_blocks, bbitmap_start, bbitmap_blocks;
    unsigned int itable_start, itable_blocks, data_start;
    unsigned int next_block, next_inode, files;
    const struct accounts *accounts;
} w;

static unsigned char itable[CACHED_ITABLE_BLOCKS][BLOCK];
static unsigned char batch[BATCH_BLOCKS * BLOCK];
static unsigned char arena[ARENA_BYTES]; /* directory entries, used like a stack */
static unsigned int arena_used;

static int write_blocks(unsigned int block, const void *data, unsigned int count)
{
    return disk_write_sectors(w.lba + (u64)block * SECTORS_PER_BLOCK, data, count * SECTORS_PER_BLOCK);
}

static int alloc_inode(unsigned int *ino)
{
    if (w.next_inode >= CACHED_ITABLE_BLOCKS * BSFS_INODES_PER_BLOCK || w.next_inode >= w.inode_count)
        return -BS_ENOSPC;
    *ino = w.next_inode++;
    return 0;
}

/* Data blocks first..first+count-1 are already written; adds the pointer tables. */
static int put_inode(unsigned int ino, unsigned int type, unsigned int size, unsigned int first,
                     unsigned int count, unsigned int perm, unsigned int uid, unsigned int gid)
{
    static unsigned int table[BSFS_PTRS_PER_BLOCK];
    struct bsfs_inode *in = (struct bsfs_inode *)(itable[ino / BSFS_INODES_PER_BLOCK] +
                                                  ino % BSFS_INODES_PER_BLOCK * BSFS_INODE_SIZE);
    unsigned int i;
    int rc;

    memset(in, 0, sizeof(*in));
    in->type = (unsigned short)type;
    in->perm = (unsigned short)perm;
    in->links = 1;
    in->size = size;
    in->uid = uid;
    in->gid = gid;
    for (i = 0; i < count && i < BSFS_DIRECT; i++)
        in->direct[i] = first + i;
    if (count > BSFS_DIRECT) {
        memset(table, 0, sizeof(table));
        for (i = BSFS_DIRECT; i < count && i < BSFS_DIRECT + BSFS_PTRS_PER_BLOCK; i++)
            table[i - BSFS_DIRECT] = first + i;
        in->indirect = w.next_block++;
        rc = write_blocks(in->indirect, table, 1);
        if (rc < 0)
            return rc;
    }
    if (count > BSFS_DIRECT + BSFS_PTRS_PER_BLOCK) {
        static unsigned int top[BSFS_PTRS_PER_BLOCK];
        unsigned int done = BSFS_DIRECT + BSFS_PTRS_PER_BLOCK, t = 0;

        memset(top, 0, sizeof(top));
        while (done < count && t < BSFS_PTRS_PER_BLOCK) {
            memset(table, 0, sizeof(table));
            for (i = 0; i < BSFS_PTRS_PER_BLOCK && done < count; i++)
                table[i] = first + done++;
            top[t++] = w.next_block++;
            rc = write_blocks(top[t - 1], table, 1);
            if (rc < 0)
                return rc;
        }
        in->double_indirect = w.next_block++;
        rc = write_blocks(in->double_indirect, top, 1);
        if (rc < 0)
            return rc;
    }
    return w.next_block <= w.total ? 0 : -BS_ENOSPC;
}

/* Writes bytes as a new file's contiguous data. */
static int put_data(const void *data, unsigned int length, unsigned int *first, unsigned int *count)
{
    unsigned int blocks = (length + BLOCK - 1) / BLOCK;
    int rc = 0;

    *first = w.next_block;
    *count = blocks;
    if (w.next_block + blocks > w.total)
        return -BS_ENOSPC;
    for (unsigned int done = 0; done < blocks && rc == 0; done += BATCH_BLOCKS) {
        unsigned int n = blocks - done < BATCH_BLOCKS ? blocks - done : BATCH_BLOCKS;
        unsigned int offset = done * BLOCK, part = length - offset < n * BLOCK ? length - offset : n * BLOCK;

        memset(batch, 0, n * BLOCK);
        memcpy(batch, (const unsigned char *)data + offset, part);
        rc = write_blocks(w.next_block + done, batch, n);
    }
    w.next_block += blocks;
    return rc;
}

static int copy_file(const char *path, unsigned int ino, const struct bs_stat *st)
{
    unsigned int first = w.next_block, count = 0, size = 0;
    int fd = open(path, BS_O_READ), got, rc = 0;

    if (fd < 0)
        return fd;
    while ((got = read(fd, batch, sizeof(batch))) > 0) {
        unsigned int n = ((unsigned int)got + BLOCK - 1) / BLOCK;

        if (w.next_block + n > w.total) {
            rc = -BS_ENOSPC;
            break;
        }
        memset(batch + got, 0, n * BLOCK - (unsigned int)got);
        rc = write_blocks(w.next_block, batch, n);
        if (rc < 0)
            break;
        w.next_block += n;
        count += n;
        size += (unsigned int)got;
    }
    close(fd);
    if (rc == 0 && got < 0)
        rc = got;
    if (rc == 0)
        rc = put_inode(ino, BSFS_TYPE_FILE, size, first, count, st->perm, st->uid, st->gid);
    w.files++;
    return rc;
}

static int put_text(unsigned int ino, const char *text, unsigned int perm)
{
    unsigned int first, count;
    int rc = put_data(text, strlen(text), &first, &count);

    w.files++;
    return rc < 0 ? rc : put_inode(ino, BSFS_TYPE_FILE, strlen(text), first, count, perm, 0, 0);
}

static void add(char *text, const char *s)
{
    memcpy(text + strlen(text), s, strlen(s) + 1);
}

static void add_number(char *text, unsigned int value)
{
    char digits[12];
    int n = 0;
    unsigned int used = strlen(text);

    do {
        digits[n++] = (char)('0' + value % 10);
        value /= 10;
    } while (value);
    while (n)
        text[used++] = digits[--n];
    text[used] = 0;
}

/* The new system's accounts replace the live ones. */
static int put_account_file(const char *name, unsigned int ino)
{
    static char text[1024];
    const struct accounts *a = w.accounts;

    text[0] = 0;
    if (strcmp(name, "passwd") == 0) {
        add(text, "# name:uid:gid:home:shell. Passwords are in /conf/shadow, which only root can read.\n");
        add(text, "root:0:0:/root:/bin/bsh.bs\n");
        add(text, a->name);
        add(text, ":1000:1000:/" HOMES "/");
        add(text, a->name);
        add(text, ":/bin/bsh.bs\n");
        return put_text(ino, text, 0644);
    }
    if (strcmp(name, "group") == 0) {
        add(text, "# name:gid:\nroot:0:\n");
        add(text, a->name);
        add(text, ":1000:\n");
        return put_text(ino, text, 0644);
    }
    add(text, "root:");
    add(text, a->root_record);
    add(text, "\n");
    add(text, a->name);
    add(text, ":");
    add(text, a->user_record);
    add(text, "\n");
    return put_text(ino, text, 0600);
}

struct child {
    unsigned int ino, type;
    char name[BS_NAME_MAX];
};

static int write_directory(unsigned int ino, struct child *children, unsigned int count, unsigned int perm,
                           unsigned int uid, unsigned int gid)
{
    unsigned int bytes = count * BSFS_DIRENT_SIZE, blocks = bytes ? (bytes + BLOCK - 1) / BLOCK : 1;
    unsigned int first = w.next_block;
    int rc = 0;

    if (w.next_block + blocks > w.total)
        return -BS_ENOSPC;
    for (unsigned int b = 0; b < blocks && rc == 0; b++) {
        memset(batch, 0, BLOCK);
        for (unsigned int s = 0; s < BSFS_DIRENTS_PER_BLOCK && b * BSFS_DIRENTS_PER_BLOCK + s < count; s++) {
            struct child *c = &children[b * BSFS_DIRENTS_PER_BLOCK + s];
            struct bsfs_dirent *d = (struct bsfs_dirent *)(batch + s * BSFS_DIRENT_SIZE);

            d->ino = c->ino;
            d->type = (unsigned char)c->type;
            d->name_len = (unsigned char)strlen(c->name);
            memcpy(d->name, c->name, d->name_len);
        }
        rc = write_blocks(w.next_block++, batch, 1);
    }
    return rc < 0 ? rc : put_inode(ino, BSFS_TYPE_DIR, blocks * BLOCK, first, blocks, perm, uid, gid);
}

static int join(char *out, const char *dir, const char *name)
{
    unsigned int a = strlen(dir), b = strlen(name);

    if (a + b + 2 > BS_PATH_MAX)
        return -BS_ENAMETOOLONG;
    memcpy(out, dir, a);
    out[a] = a && dir[a - 1] == '/' ? 0 : '/';
    memcpy(out + a + (out[a] ? 1 : 0), name, b + 1);
    return 0;
}

static int copy_dir(const char *path, unsigned int ino, int top);

/* /userspace gets just the new account's home; the live one stays behind. */
static int make_homes(unsigned int ino)
{
    struct child *home = (struct child *)(arena + arena_used);
    int rc;

    if (arena_used + sizeof(*home) > ARENA_BYTES)
        return -BS_ENOMEM;
    arena_used += sizeof(*home);
    rc = alloc_inode(&home->ino);
    home->type = BSFS_TYPE_DIR;
    memcpy(home->name, w.accounts->name, strlen(w.accounts->name) + 1);
    if (rc == 0)
        rc = write_directory(home->ino, 0, 0, 0700, 1000, 1000);
    if (rc == 0)
        rc = write_directory(ino, home, 1, 0755, 0, 0);
    arena_used -= sizeof(*home);
    return rc;
}

static int copy_dir(const char *path, unsigned int ino, int top)
{
    struct child *children = (struct child *)(arena + arena_used);
    unsigned int count = 0, saved = arena_used;
    struct bs_dirent entry;
    struct bs_stat st;
    char child_path[BS_PATH_MAX], target[BS_PATH_MAX];
    int fd = open(path, BS_O_READ), rc = 0;

    if (fd < 0)
        return fd;
    while ((rc = readdir(fd, &entry)) > 0) {
        if (arena_used + sizeof(struct child) > ARENA_BYTES) {
            rc = -BS_ENOMEM;
            break;
        }
        arena_used += sizeof(struct child);
        memcpy(children[count].name, entry.name, strlen(entry.name) + 1);
        children[count].type = entry.type;
        count++;
    }
    close(fd);

    for (unsigned int i = 0; i < count && rc == 0; i++) {
        struct child *c = &children[i];

        rc = alloc_inode(&c->ino);
        if (rc == 0)
            rc = join(child_path, path, c->name);
        if (rc < 0)
            break;
        if (c->type == BS_TYPE_SYMLINK) {
            unsigned int first, blocks;
            int length = readlink(child_path, target, sizeof(target));

            rc = length < 0 ? length : put_data(target, (unsigned int)length, &first, &blocks);
            if (rc == 0)
                rc = put_inode(c->ino, BSFS_TYPE_SYMLINK, (unsigned int)length, first, blocks, 0777, 0, 0);
        } else if (top && strcmp(c->name, HOMES) == 0 && c->type == BS_TYPE_DIR) {
            rc = make_homes(c->ino);
        } else if (top && strcmp(c->name, "root") == 0 && c->type == BS_TYPE_DIR) {
            rc = write_directory(c->ino, 0, 0, 0700, 0, 0); /* a fresh, empty /root */
        } else if (strcmp(path, "/conf") == 0 && (strcmp(c->name, "passwd") == 0 || strcmp(c->name, "group") == 0 ||
                                                   strcmp(c->name, "shadow") == 0)) {
            rc = put_account_file(c->name, c->ino);
        } else {
            rc = stat(child_path, &st);
            if (rc == 0 && c->type == BS_TYPE_DIR)
                rc = copy_dir(child_path, c->ino, 0) == 0 ? 0 : -BS_EIO;
            else if (rc == 0)
                rc = copy_file(child_path, c->ino, &st);
            if (rc == 0 && c->type == BS_TYPE_DIR) /* copy_dir wrote it with the right mode below */
                continue;
        }
        if (rc == 0 && (w.files & 7) == 0) {
            char line[64] = "copying: ";

            add_number(line, w.files);
            add(line, " files");
            progress(line);
        }
    }
    if (rc == 0) {
        if (stat(path, &st) < 0)
            st.perm = 0755, st.uid = st.gid = 0;
        rc = write_directory(ino, children, count, st.perm, st.uid, st.gid);
    }
    arena_used = saved;
    return rc;
}

static int write_bitmap(unsigned int start, unsigned int blocks, unsigned int used)
{
    for (unsigned int b = 0; b < blocks; b++) {
        unsigned int base = b * BLOCK * 8;
        int rc;

        memset(batch, 0, BLOCK);
        for (unsigned int bit = 0; bit < BLOCK * 8 && base + bit < used; bit++)
            batch[bit / 8] |= (unsigned char)(1u << (bit % 8));
        rc = write_blocks(start + b, batch, 1);
        if (rc < 0)
            return rc;
    }
    return 0;
}

int bsfs_install(const struct plan *plan, const struct accounts *accounts, unsigned int *files)
{
    struct bsfs_super *sb = (struct bsfs_super *)batch;
    unsigned int root;
    int rc;

    memset(&w, 0, sizeof(w));
    memset(itable, 0, sizeof(itable));
    w.accounts = accounts;
    w.lba = plan->bsfs_lba;
    w.total = (unsigned int)(plan->bsfs_sectors / SECTORS_PER_BLOCK);
    w.inode_count = w.total / 8 > MAX_INODES ? MAX_INODES : w.total / 8 < MIN_INODES ? MIN_INODES : w.total / 8;
    w.inode_count &= ~(BSFS_INODES_PER_BLOCK - 1);
    w.ibitmap_start = 1;
    w.ibitmap_blocks = (w.inode_count + BLOCK * 8 - 1) / (BLOCK * 8);
    w.bbitmap_start = w.ibitmap_start + w.ibitmap_blocks;
    w.bbitmap_blocks = (w.total + BLOCK * 8 - 1) / (BLOCK * 8);
    w.itable_start = w.bbitmap_start + w.bbitmap_blocks;
    w.itable_blocks = w.inode_count / BSFS_INODES_PER_BLOCK;
    w.data_start = w.itable_start + w.itable_blocks;
    w.next_block = w.data_start;
    w.next_inode = 1; /* inode 0 is never used */

    rc = alloc_inode(&root);
    if (rc == 0)
        rc = copy_dir("/", root, 1);
    if (rc < 0)
        return rc;

    /* The inode table: the cached part, then zeros for the rest. */
    rc = write_blocks(w.itable_start, itable, CACHED_ITABLE_BLOCKS);
    memset(batch, 0, sizeof(batch));
    for (unsigned int b = CACHED_ITABLE_BLOCKS; b < w.itable_blocks && rc == 0; b += BATCH_BLOCKS) {
        unsigned int n = w.itable_blocks - b < BATCH_BLOCKS ? w.itable_blocks - b : BATCH_BLOCKS;

        rc = write_blocks(w.itable_start + b, batch, n);
    }
    if (rc == 0)
        rc = write_bitmap(w.ibitmap_start, w.ibitmap_blocks, w.next_inode);
    if (rc == 0)
        rc = write_bitmap(w.bbitmap_start, w.bbitmap_blocks, w.next_block);
    if (rc < 0)
        return rc;

    memset(batch, 0, BLOCK);
    sb->magic = BSFS_MAGIC;
    sb->version = BSFS_VERSION;
    sb->block_size = BLOCK;
    sb->total_blocks = w.total;
    sb->inode_count = w.inode_count;
    sb->free_blocks = w.total - w.next_block;
    sb->free_inodes = w.inode_count - w.next_inode;
    sb->ibitmap_start = w.ibitmap_start;
    sb->ibitmap_blocks = w.ibitmap_blocks;
    sb->bbitmap_start = w.bbitmap_start;
    sb->bbitmap_blocks = w.bbitmap_blocks;
    sb->itable_start = w.itable_start;
    sb->itable_blocks = w.itable_blocks;
    sb->data_start = w.data_start;
    sb->root_ino = root;
    *files = w.files;
    return write_blocks(0, batch, 1); /* last: the filesystem only exists once this lands */
}
