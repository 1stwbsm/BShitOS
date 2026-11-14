#include <bshit/bsfs.h>

#define BLOCK_BUF(name) uint8_t name[BSFS_BLOCK_SIZE] __attribute__((aligned(16)))

int bsfs_mount(struct bsfs *fs)
{
    BLOCK_BUF(block);

    if (fs->read_block(fs->ctx, 0, block) < 0)
        return -1;
    __builtin_memcpy(&fs->sb, block, sizeof(fs->sb));
    if (fs->sb.magic != BSFS_MAGIC || fs->sb.version != BSFS_VERSION ||
        fs->sb.block_size != BSFS_BLOCK_SIZE)
        return -1;
    return 0;
}

int bsfs_read_inode(struct bsfs *fs, uint32_t ino, struct bsfs_inode *out)
{
    BLOCK_BUF(block);

    if (ino == 0 || ino >= fs->sb.inode_count)
        return -1;
    if (fs->read_block(fs->ctx, fs->sb.itable_start + ino / BSFS_INODES_PER_BLOCK, block) < 0)
        return -1;
    __builtin_memcpy(out, block + ino % BSFS_INODES_PER_BLOCK * BSFS_INODE_SIZE, sizeof(*out));
    return 0;
}

static int table_entry(struct bsfs *fs, uint32_t table, uint32_t index, uint32_t *out)
{
    BLOCK_BUF(block);

    if (table == 0) {
        *out = 0;
        return 0;
    }
    if (fs->read_block(fs->ctx, table, block) < 0)
        return -1;
    *out = ((uint32_t *)block)[index];
    return 0;
}

int bsfs_bmap(struct bsfs *fs, const struct bsfs_inode *in, uint32_t index, uint32_t *block)
{
    uint32_t table;

    if (index < BSFS_DIRECT) {
        *block = in->direct[index];
        return 0;
    }
    index -= BSFS_DIRECT;
    if (index < BSFS_PTRS_PER_BLOCK)
        return table_entry(fs, in->indirect, index, block);
    index -= BSFS_PTRS_PER_BLOCK;
    if (index / BSFS_PTRS_PER_BLOCK >= BSFS_PTRS_PER_BLOCK)
        return -1;
    if (table_entry(fs, in->double_indirect, index / BSFS_PTRS_PER_BLOCK, &table) < 0)
        return -1;
    return table_entry(fs, table, index % BSFS_PTRS_PER_BLOCK, block);
}

int bsfs_read(struct bsfs *fs, const struct bsfs_inode *in, uint64_t off, void *buf, uint32_t len)
{
    BLOCK_BUF(block);
    uint8_t *dst = buf;
    uint32_t done = 0;

    if (off >= in->size)
        return 0;
    if (len > in->size - off)
        len = (uint32_t)(in->size - off);

    while (done < len) {
        uint32_t index = (uint32_t)(off / BSFS_BLOCK_SIZE);
        uint32_t within = (uint32_t)off & (BSFS_BLOCK_SIZE - 1);
        uint32_t chunk = BSFS_BLOCK_SIZE - within, physical;

        if (chunk > len - done)
            chunk = len - done;
        if (bsfs_bmap(fs, in, index, &physical) < 0)
            return -1;
        if (physical == 0)
            __builtin_memset(block, 0, BSFS_BLOCK_SIZE);
        else if (fs->read_block(fs->ctx, physical, block) < 0)
            return -1;
        __builtin_memcpy(dst + done, block + within, chunk);
        done += chunk;
        off += chunk;
    }
    return (int)done;
}

int bsfs_next_dirent(struct bsfs *fs, const struct bsfs_inode *dir, uint32_t *cursor,
                     struct bsfs_dirent *out)
{
    for (;;) {
        int got = bsfs_read(fs, dir, (uint64_t)*cursor * BSFS_DIRENT_SIZE, out, BSFS_DIRENT_SIZE);

        if (got < 0)
            return -1;
        if (got < (int)BSFS_DIRENT_SIZE)
            return 0;
        (*cursor)++;
        if (out->ino != 0)
            return 1;
    }
}

static int name_equal(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

int bsfs_lookup(struct bsfs *fs, const struct bsfs_inode *dir, const char *name, uint32_t *ino)
{
    struct bsfs_dirent entry;
    uint32_t cursor = 0;
    int result;

    while ((result = bsfs_next_dirent(fs, dir, &cursor, &entry)) > 0) {
        if (name_equal(entry.name, name)) {
            *ino = entry.ino;
            return 1;
        }
    }
    return result;
}
