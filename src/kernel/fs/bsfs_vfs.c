/* BSFS VFS backend. Reads use the shared core; every write goes straight to disk. */
#include <bshit/bsfs.h>

#include "drivers/ahci.h"
#include "fs/partitions.h"
#include "fs/vfs.h"
#include "lib/string.h"

#define SECTORS_PER_BLOCK (BSFS_BLOCK_SIZE / AHCI_SECTOR_SIZE)
#define BLOCK_BUF(name) uint8_t name[BSFS_BLOCK_SIZE] __attribute__((aligned(16)))

static uint64_t partition_lba;
static uint8_t *ram_base;
static uint32_t ram_size;
static struct bsfs fs;
static const struct fs_ops bsfs_ops;

static int disk_read(void *ctx, uint32_t block, void *buf)
{
    (void)ctx;
    if (ram_base) {
        if ((uint64_t)block * BSFS_BLOCK_SIZE + BSFS_BLOCK_SIZE > ram_size)
            return -1;
        memcpy(buf, ram_base + block * BSFS_BLOCK_SIZE, BSFS_BLOCK_SIZE);
        return 0;
    }
    return ahci_read(partition_lba + (uint64_t)block * SECTORS_PER_BLOCK, SECTORS_PER_BLOCK, buf);
}

static int disk_write(void *ctx, uint32_t block, const void *buf)
{
    (void)ctx;
    if (ram_base) {
        if ((uint64_t)block * BSFS_BLOCK_SIZE + BSFS_BLOCK_SIZE > ram_size)
            return -1;
        memcpy(ram_base + block * BSFS_BLOCK_SIZE, buf, BSFS_BLOCK_SIZE);
        return 0;
    }
    return ahci_write(partition_lba + (uint64_t)block * SECTORS_PER_BLOCK, SECTORS_PER_BLOCK, buf);
}

static int write_inode(uint32_t ino, const struct bsfs_inode *in)
{
    BLOCK_BUF(block);
    uint32_t number = fs.sb.itable_start + ino / BSFS_INODES_PER_BLOCK;

    if (disk_read(0, number, block) < 0)
        return -BS_EIO;
    memcpy(block + ino % BSFS_INODES_PER_BLOCK * BSFS_INODE_SIZE, in, sizeof(*in));
    return disk_write(0, number, block) < 0 ? -BS_EIO : 0;
}

static int write_super(void)
{
    BLOCK_BUF(block);

    memset(block, 0, sizeof(block));
    memcpy(block, &fs.sb, sizeof(fs.sb));
    return disk_write(0, 0, block) < 0 ? -BS_EIO : 0;
}

/* Finds a clear bit, sets it on disk and returns it through *out. */
static int bitmap_alloc(uint32_t start, uint32_t blocks, uint32_t limit, uint32_t *out)
{
    BLOCK_BUF(block);

    for (uint32_t b = 0; b < blocks; b++) {
        if (disk_read(0, start + b, block) < 0)
            return -BS_EIO;
        for (uint32_t bit = 0; bit < BSFS_BLOCK_SIZE * 8; bit++) {
            uint32_t index = b * BSFS_BLOCK_SIZE * 8 + bit;

            if (index >= limit)
                return -BS_ENOSPC;
            if (block[bit / 8] & (1u << (bit % 8)))
                continue;
            block[bit / 8] |= (uint8_t)(1u << (bit % 8));
            if (disk_write(0, start + b, block) < 0)
                return -BS_EIO;
            *out = index;
            return 0;
        }
    }
    return -BS_ENOSPC;
}

static int bitmap_clear(uint32_t start, uint32_t index)
{
    BLOCK_BUF(block);
    uint32_t number = start + index / (BSFS_BLOCK_SIZE * 8);
    uint32_t bit = index % (BSFS_BLOCK_SIZE * 8);

    if (disk_read(0, number, block) < 0)
        return -BS_EIO;
    block[bit / 8] &= (uint8_t)~(1u << (bit % 8));
    return disk_write(0, number, block) < 0 ? -BS_EIO : 0;
}

static int alloc_block(uint32_t *out)
{
    BLOCK_BUF(zero);
    int rc = bitmap_alloc(fs.sb.bbitmap_start, fs.sb.bbitmap_blocks, fs.sb.total_blocks, out);

    if (rc < 0)
        return rc;
    fs.sb.free_blocks--;
    memset(zero, 0, sizeof(zero));
    if (disk_write(0, *out, zero) < 0)
        return -BS_EIO;
    return write_super();
}

static int free_block(uint32_t number)
{
    int rc = bitmap_clear(fs.sb.bbitmap_start, number);

    if (rc < 0)
        return rc;
    fs.sb.free_blocks++;
    return write_super();
}

/* Returns the block a table points to at index, allocating it if needed. */
static int table_slot(uint32_t table, uint32_t index, uint32_t *out)
{
    uint32_t entries[BSFS_PTRS_PER_BLOCK];
    int rc;

    if (disk_read(0, table, entries) < 0)
        return -BS_EIO;
    if (!entries[index]) {
        rc = alloc_block(&entries[index]);
        if (rc < 0)
            return rc;
        if (disk_write(0, table, entries) < 0)
            return -BS_EIO;
    }
    *out = entries[index];
    return 0;
}

/* Takes a value, not a pointer: inode fields are packed. */
static int ensure(uint32_t current, uint32_t *out)
{
    *out = current;
    return current ? 0 : alloc_block(out);
}

/* Like bsfs_bmap but allocates. The caller must write the inode afterwards. */
static int bmap_alloc(struct bsfs_inode *in, uint32_t index, uint32_t *out)
{
    uint32_t table, root;
    int rc;

    if (index < BSFS_DIRECT) {
        rc = ensure(in->direct[index], out);
        if (rc == 0)
            in->direct[index] = *out;
        return rc;
    }
    index -= BSFS_DIRECT;
    if (index < BSFS_PTRS_PER_BLOCK) {
        rc = ensure(in->indirect, &root);
        if (rc < 0)
            return rc;
        in->indirect = root;
        return table_slot(root, index, out);
    }
    index -= BSFS_PTRS_PER_BLOCK;
    if (index / BSFS_PTRS_PER_BLOCK >= BSFS_PTRS_PER_BLOCK)
        return -BS_ENOSPC;
    rc = ensure(in->double_indirect, &root);
    if (rc < 0)
        return rc;
    in->double_indirect = root;
    rc = table_slot(root, index / BSFS_PTRS_PER_BLOCK, &table);
    return rc < 0 ? rc : table_slot(table, index % BSFS_PTRS_PER_BLOCK, out);
}

static int free_table(uint32_t table, int levels)
{
    uint32_t entries[BSFS_PTRS_PER_BLOCK];
    int rc;

    if (!table)
        return 0;
    if (levels > 0) {
        if (disk_read(0, table, entries) < 0)
            return -BS_EIO;
        for (uint32_t i = 0; i < BSFS_PTRS_PER_BLOCK; i++) {
            rc = free_table(entries[i], levels - 1);
            if (rc < 0)
                return rc;
        }
    }
    return free_block(table);
}

static int free_contents(struct bsfs_inode *in)
{
    int rc;

    for (uint32_t i = 0; i < BSFS_DIRECT; i++) {
        rc = free_table(in->direct[i], 0);
        if (rc < 0)
            return rc;
        in->direct[i] = 0;
    }
    rc = free_table(in->indirect, 1);
    if (rc == 0)
        rc = free_table(in->double_indirect, 2);
    if (rc < 0)
        return rc;
    in->indirect = in->double_indirect = 0;
    in->size = 0;
    return 0;
}

static void fill_vnode(uint32_t ino, const struct bsfs_inode *in, struct vnode *out)
{
    out->ops = &bsfs_ops;
    out->fs = &fs;
    out->id = ino;
    out->type = in->type;
    out->size = (uint32_t)in->size;
}

/* ---- VFS operations --------------------------------------------------- */

static int op_lookup(const struct vnode *dir, const char *name, struct vnode *out)
{
    struct bsfs_inode in;
    uint32_t ino;
    int rc;

    if (bsfs_read_inode(&fs, dir->id, &in) < 0)
        return -BS_EIO;
    rc = bsfs_lookup(&fs, &in, name, &ino);
    if (rc < 0)
        return -BS_EIO;
    if (rc == 0)
        return -BS_ENOENT;
    if (bsfs_read_inode(&fs, ino, &in) < 0)
        return -BS_EIO;
    fill_vnode(ino, &in, out);
    return 0;
}

static int op_read(const struct vnode *file, uint32_t off, void *buf, uint32_t len)
{
    struct bsfs_inode in;
    int got;

    if (bsfs_read_inode(&fs, file->id, &in) < 0)
        return -BS_EIO;
    got = bsfs_read(&fs, &in, off, buf, len);
    return got < 0 ? -BS_EIO : got;
}

static int op_readdir(const struct vnode *dir, uint32_t *cursor, struct bs_dirent *out)
{
    struct bsfs_inode in, child;
    struct bsfs_dirent entry;
    int rc;

    if (bsfs_read_inode(&fs, dir->id, &in) < 0)
        return -BS_EIO;
    rc = bsfs_next_dirent(&fs, &in, cursor, &entry);
    if (rc < 0)
        return -BS_EIO;
    if (rc == 0)
        return 0;
    if (bsfs_read_inode(&fs, entry.ino, &child) < 0)
        return -BS_EIO;
    strlcpy_checked(out->name, entry.name, BS_NAME_MAX);
    out->type = child.type;
    out->size = (uint32_t)child.size;
    return 1;
}

static int op_readlink(const struct vnode *link, char *buf, uint32_t size)
{
    int got;

    if (link->size + 1 > size)
        return -BS_ENAMETOOLONG;
    got = op_read(link, 0, buf, size - 1);
    if (got < 0)
        return got;
    buf[got] = 0;
    return got;
}

/* Finds a free slot, growing the directory by a block if there is none. */
static int add_entry(uint32_t dir_ino, const struct bsfs_dirent *entry)
{
    BLOCK_BUF(block);
    struct bsfs_inode dir;
    uint32_t blocks, physical;
    int rc;

    if (bsfs_read_inode(&fs, dir_ino, &dir) < 0)
        return -BS_EIO;
    blocks = (uint32_t)(dir.size / BSFS_BLOCK_SIZE);

    for (uint32_t i = 0; i < blocks; i++) {
        struct bsfs_dirent *slots = (struct bsfs_dirent *)block;

        if (bsfs_bmap(&fs, &dir, i, &physical) < 0 || disk_read(0, physical, block) < 0)
            return -BS_EIO;
        for (uint32_t s = 0; s < BSFS_DIRENTS_PER_BLOCK; s++) {
            if (slots[s].ino == 0) {
                slots[s] = *entry;
                return disk_write(0, physical, block) < 0 ? -BS_EIO : 0;
            }
        }
    }

    rc = bmap_alloc(&dir, blocks, &physical);
    if (rc < 0)
        return rc;
    memset(block, 0, sizeof(block));
    memcpy(block, entry, sizeof(*entry));
    if (disk_write(0, physical, block) < 0)
        return -BS_EIO;
    dir.size += BSFS_BLOCK_SIZE;
    return write_inode(dir_ino, &dir);
}

static int op_create(const struct vnode *dir, const char *name, uint32_t type, struct vnode *out)
{
    struct bsfs_inode parent, in;
    struct bsfs_dirent entry;
    uint32_t ino, length = strlen(name);
    int rc;

    if (length == 0 || length > BSFS_NAME_MAX)
        return -BS_ENAMETOOLONG;
    if (bsfs_read_inode(&fs, dir->id, &parent) < 0)
        return -BS_EIO;
    rc = bsfs_lookup(&fs, &parent, name, &ino);
    if (rc < 0)
        return -BS_EIO;
    if (rc == 1)
        return -BS_EEXIST;

    rc = bitmap_alloc(fs.sb.ibitmap_start, fs.sb.ibitmap_blocks, fs.sb.inode_count, &ino);
    if (rc < 0)
        return rc;
    fs.sb.free_inodes--;
    rc = write_super();
    if (rc < 0)
        return rc;

    memset(&in, 0, sizeof(in));
    in.type = (uint16_t)type;
    in.perm = 0755;
    in.links = 1;
    if (type == BSFS_TYPE_DIR) {
        uint32_t first;

        rc = bmap_alloc(&in, 0, &first);
        if (rc < 0)
            return rc;
        in.size = BSFS_BLOCK_SIZE;
    }
    rc = write_inode(ino, &in);
    if (rc < 0)
        return rc;

    memset(&entry, 0, sizeof(entry));
    entry.ino = ino;
    entry.type = (uint8_t)type;
    entry.name_len = (uint8_t)length;
    memcpy(entry.name, name, length);
    rc = add_entry(dir->id, &entry);
    if (rc < 0)
        return rc;
    fill_vnode(ino, &in, out);
    return 0;
}

static int op_write(struct vnode *file, uint32_t off, const void *buf, uint32_t len)
{
    BLOCK_BUF(block);
    struct bsfs_inode in;
    const uint8_t *src = buf;
    uint32_t done = 0;
    int rc = 0;

    if (bsfs_read_inode(&fs, file->id, &in) < 0)
        return -BS_EIO;

    while (done < len) {
        uint32_t position = off + done;
        uint32_t within = position % BSFS_BLOCK_SIZE;
        uint32_t chunk = BSFS_BLOCK_SIZE - within, physical;

        if (chunk > len - done)
            chunk = len - done;
        rc = bmap_alloc(&in, position / BSFS_BLOCK_SIZE, &physical);
        if (rc < 0)
            break;
        if (chunk < BSFS_BLOCK_SIZE && disk_read(0, physical, block) < 0) {
            rc = -BS_EIO;
            break;
        }
        memcpy(block + within, src + done, chunk);
        if (disk_write(0, physical, block) < 0) {
            rc = -BS_EIO;
            break;
        }
        done += chunk;
        if (position + chunk > in.size)
            in.size = position + chunk;
    }

    {
        int saved = write_inode(file->id, &in);

        if (saved < 0)
            return saved;
    }
    file->size = (uint32_t)in.size;
    return done || rc == 0 ? (int)done : rc;
}

static int op_truncate(struct vnode *file)
{
    struct bsfs_inode in;
    int rc;

    if (bsfs_read_inode(&fs, file->id, &in) < 0)
        return -BS_EIO;
    rc = free_contents(&in);
    if (rc < 0)
        return rc;
    file->size = 0;
    return write_inode(file->id, &in);
}

static int op_unlink(const struct vnode *dir, const char *name)
{
    BLOCK_BUF(block);
    struct bsfs_inode parent, in;
    struct bsfs_dirent probe;
    uint32_t ino, cursor = 0;
    int rc;

    if (bsfs_read_inode(&fs, dir->id, &parent) < 0)
        return -BS_EIO;
    rc = bsfs_lookup(&fs, &parent, name, &ino);
    if (rc < 0)
        return -BS_EIO;
    if (rc == 0)
        return -BS_ENOENT;
    if (bsfs_read_inode(&fs, ino, &in) < 0)
        return -BS_EIO;
    if (in.type == BSFS_TYPE_DIR) {
        rc = bsfs_next_dirent(&fs, &in, &cursor, &probe);
        if (rc != 0)
            return rc < 0 ? -BS_EIO : -BS_ENOTEMPTY;
    }

    /* Remove the name first: a crash then leaks blocks, not corrupts files. */
    for (uint32_t i = 0; i < parent.size / BSFS_BLOCK_SIZE; i++) {
        struct bsfs_dirent *slots = (struct bsfs_dirent *)block;
        uint32_t physical;
        int removed = 0;

        if (bsfs_bmap(&fs, &parent, i, &physical) < 0 || disk_read(0, physical, block) < 0)
            return -BS_EIO;
        for (uint32_t s = 0; s < BSFS_DIRENTS_PER_BLOCK && !removed; s++) {
            if (slots[s].ino == ino) {
                memset(&slots[s], 0, sizeof(slots[s]));
                removed = 1;
            }
        }
        if (removed) {
            if (disk_write(0, physical, block) < 0)
                return -BS_EIO;
            break;
        }
    }

    rc = free_contents(&in);
    if (rc < 0)
        return rc;
    in.type = BSFS_TYPE_FREE;
    rc = write_inode(ino, &in);
    if (rc < 0)
        return rc;
    rc = bitmap_clear(fs.sb.ibitmap_start, ino);
    if (rc < 0)
        return rc;
    fs.sb.free_inodes++;
    return write_super();
}

static const struct fs_ops bsfs_ops = {
    .lookup = op_lookup,
    .read = op_read,
    .readdir = op_readdir,
    .readlink = op_readlink,
    .create = op_create,
    .write = op_write,
    .truncate = op_truncate,
    .unlink = op_unlink,
};

void bsfs_usage(uint32_t *total_kib, uint32_t *free_kib)
{
    *total_kib = fs.sb.total_blocks * (BSFS_BLOCK_SIZE / 1024);
    *free_kib = fs.sb.free_blocks * (BSFS_BLOCK_SIZE / 1024);
}

int bsfs_mount_ram_volume(const char *name, void *base, uint32_t size)
{
    ram_base = base;
    ram_size = size;
    return bsfs_mount_volume(name, 0);
}

int bsfs_mount_volume(const char *name, uint64_t lba)
{
    struct bsfs_inode root;
    struct vnode node;

    partition_lba = lba;
    fs.read_block = disk_read;
    fs.write_block = disk_write;
    fs.ctx = 0;
    if (bsfs_mount(&fs) < 0 || bsfs_read_inode(&fs, fs.sb.root_ino, &root) < 0)
        return -BS_EIO;
    fill_vnode(fs.sb.root_ino, &root, &node);
    return vfs_add_volume(name, &node);
}
