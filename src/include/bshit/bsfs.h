/* BSFS on-disk format and read-only core, shared by BSBOOT and the kernel. */
#ifndef BSHIT_BSFS_H
#define BSHIT_BSFS_H

#include <stdint.h>

#define BSFS_MAGIC      0x53465342u /* "BSFS" */
#define BSFS_VERSION    1u
#define BSFS_BLOCK_SIZE 4096u
#define BSFS_ROOT_INO   1u
#define BSFS_DIRECT     12u
#define BSFS_PTRS_PER_BLOCK (BSFS_BLOCK_SIZE / 4u)
#define BSFS_INODE_SIZE 128u
#define BSFS_INODES_PER_BLOCK (BSFS_BLOCK_SIZE / BSFS_INODE_SIZE)
#define BSFS_DIRENT_SIZE 64u
#define BSFS_DIRENTS_PER_BLOCK (BSFS_BLOCK_SIZE / BSFS_DIRENT_SIZE)
#define BSFS_NAME_MAX   57u

enum bsfs_type {
    BSFS_TYPE_FREE    = 0,
    BSFS_TYPE_FILE    = 1,
    BSFS_TYPE_DIR     = 2,
    BSFS_TYPE_SYMLINK = 3,
};

struct bsfs_super {
    uint32_t magic, version, block_size, total_blocks;
    uint32_t inode_count, free_blocks, free_inodes;
    uint32_t ibitmap_start, ibitmap_blocks;
    uint32_t bbitmap_start, bbitmap_blocks;
    uint32_t itable_start, itable_blocks;
    uint32_t data_start, root_ino;
} __attribute__((packed));

/* perm holds rwx bits for owner, group and others (0755 style). uid and gid
 * used to be reserved bytes, so older images read as owned by root. */
struct bsfs_inode {
    uint16_t type, perm;
    uint32_t links;
    uint64_t size, mtime;
    uint32_t direct[BSFS_DIRECT];
    uint32_t indirect, double_indirect;
    uint32_t uid, gid;
    uint8_t reserved[40];
} __attribute__((packed));

struct bsfs_dirent {
    uint32_t ino;
    uint8_t type, name_len;
    char name[58];
} __attribute__((packed));

_Static_assert(sizeof(struct bsfs_inode) == BSFS_INODE_SIZE, "BSFS format changed");
_Static_assert(sizeof(struct bsfs_dirent) == BSFS_DIRENT_SIZE, "BSFS format changed");

struct bsfs {
    struct bsfs_super sb;
    int (*read_block)(void *ctx, uint32_t block, void *buf);
    int (*write_block)(void *ctx, uint32_t block, const void *buf); /* may be 0 */
    void *ctx;
};

/* All return 0 (or a byte count) on success and -1 on failure. */
int bsfs_mount(struct bsfs *fs);
int bsfs_read_inode(struct bsfs *fs, uint32_t ino, struct bsfs_inode *out);
/* Physical block of file block index, 0 for a hole. */
int bsfs_bmap(struct bsfs *fs, const struct bsfs_inode *in, uint32_t index, uint32_t *block);
int bsfs_read(struct bsfs *fs, const struct bsfs_inode *in, uint64_t off, void *buf, uint32_t len);
/* Returns 1 with the next used entry, 0 at the end. *cursor starts at 0. */
int bsfs_next_dirent(struct bsfs *fs, const struct bsfs_inode *dir, uint32_t *cursor,
                     struct bsfs_dirent *out);
/* Returns 1 and sets *ino if found, 0 if absent. */
int bsfs_lookup(struct bsfs *fs, const struct bsfs_inode *dir, const char *name, uint32_t *ino);

#endif
