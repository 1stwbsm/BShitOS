/* Read-only FAT32 backend, for the boot partition and USB drives. 8.3 names only. */
#include "drivers/ahci.h"
#include "fs/partitions.h"
#include "fs/vfs.h"
#include "lib/string.h"

#define SECTOR AHCI_SECTOR_SIZE
#define MAX_VOLUMES 2
#define ENTRY_SIZE 32u
#define ATTR_VOLUME 0x08
#define ATTR_DIR 0x10
#define ATTR_LONG_NAME 0x0F
#define NAME_DELETED 0xE5
#define CLUSTER_END 0x0FFFFFF8u
#define CLUSTER_MASK 0x0FFFFFFFu
#define NO_CACHE (~(uint64_t)0)
#define MBR_TABLE 446
#define MBR_ENTRY 16
#define TYPE_FAT32_CHS 0x0B
#define TYPE_FAT32_LBA 0x0C
#define TYPE_GPT 0xEE
#define GPT_ENTRIES_LBA 2

struct fat {
    fat32_read_fn read;
    uint64_t fat_lba, data_lba, cached;
    uint32_t sectors_per_cluster, root_cluster;
    uint8_t fat_sector[SECTOR] __attribute__((aligned(16)));
};

static struct fat volumes[MAX_VOLUMES];
static int volume_count;
static const struct fs_ops fat_ops;

static int next_cluster(struct fat *f, uint32_t cluster, uint32_t *next)
{
    uint64_t lba = f->fat_lba + cluster / (SECTOR / 4);
    uint32_t value;

    if (lba != f->cached) {
        if (f->read(lba, 1, f->fat_sector) < 0) {
            f->cached = NO_CACHE;
            return -BS_EIO;
        }
        f->cached = lba;
    }
    memcpy(&value, f->fat_sector + cluster % (SECTOR / 4) * 4, 4);
    *next = value & CLUSTER_MASK;
    return 0;
}

/* Reads along a cluster chain; stops early at the end of the chain. */
static int read_chain(struct fat *f, uint32_t cluster, uint32_t off, void *buf, uint32_t len)
{
    uint8_t sector[SECTOR] __attribute__((aligned(16)));
    uint32_t cluster_bytes = f->sectors_per_cluster * SECTOR;
    uint8_t *dst = buf;
    uint32_t done = 0;

    for (uint32_t skip = off / cluster_bytes; skip > 0; skip--) {
        if (cluster < 2 || cluster >= CLUSTER_END || next_cluster(f, cluster, &cluster) < 0)
            return 0;
    }
    off %= cluster_bytes;

    while (done < len && cluster >= 2 && cluster < CLUSTER_END) {
        uint64_t lba = f->data_lba + (uint64_t)(cluster - 2) * f->sectors_per_cluster + off / SECTOR;
        uint32_t within = off % SECTOR;
        uint32_t chunk = SECTOR - within;

        if (chunk > len - done)
            chunk = len - done;
        if (f->read(lba, 1, sector) < 0)
            return -BS_EIO;
        memcpy(dst + done, sector + within, chunk);
        done += chunk;
        off += chunk;
        if (off == cluster_bytes) {
            off = 0;
            if (next_cluster(f, cluster, &cluster) < 0)
                return -BS_EIO;
        }
    }
    return (int)done;
}

struct raw_entry {
    char name[11];
    uint8_t attr, ntres, tenth;
    uint16_t ctime, cdate, adate, cluster_high, wtime, wdate, cluster_low;
    uint32_t size;
} __attribute__((packed));

/* Returns 1 with the next real entry, 0 at the end. */
static int next_entry(struct fat *f, uint32_t cluster, uint32_t *cursor, struct raw_entry *e)
{
    for (;;) {
        int got = read_chain(f, cluster, *cursor, e, ENTRY_SIZE);

        if (got < 0)
            return got;
        if (got < (int)ENTRY_SIZE || e->name[0] == 0)
            return 0;
        *cursor += ENTRY_SIZE;
        if ((uint8_t)e->name[0] == NAME_DELETED || e->attr == ATTR_LONG_NAME ||
            (e->attr & ATTR_VOLUME) || e->name[0] == '.')
            continue;
        return 1;
    }
}

static void display_name(const struct raw_entry *e, char *out)
{
    uint32_t n = 0;

    for (int i = 0; i < 8 && e->name[i] != ' '; i++)
        out[n++] = e->name[i];
    if (e->name[8] != ' ') {
        out[n++] = '.';
        for (int i = 8; i < 11 && e->name[i] != ' '; i++)
            out[n++] = e->name[i];
    }
    out[n] = 0;
}

static char upper(char c)
{
    return c >= 'a' && c <= 'z' ? (char)(c - 32) : c;
}

static int names_match(const char *a, const char *b)
{
    while (*a && upper(*a) == upper(*b)) {
        a++;
        b++;
    }
    return *a == *b;
}

static int fat_lookup(const struct vnode *dir, const char *name, struct vnode *out)
{
    struct raw_entry e;
    uint32_t cursor = 0;
    int rc;

    while ((rc = next_entry(dir->fs, dir->id, &cursor, &e)) > 0) {
        char shown[13];

        display_name(&e, shown);
        if (names_match(shown, name)) {
            out->ops = &fat_ops;
            out->fs = dir->fs;
            out->id = (uint32_t)e.cluster_high << 16 | e.cluster_low;
            out->type = e.attr & ATTR_DIR ? BS_TYPE_DIR : BS_TYPE_FILE;
            out->size = e.size;
            out->owned = 0; /* no owners or permission bits here */
            return 0;
        }
    }
    return rc < 0 ? rc : -BS_ENOENT;
}

static int fat_read(const struct vnode *file, uint32_t off, void *buf, uint32_t len)
{
    if (off >= file->size)
        return 0;
    if (len > file->size - off)
        len = file->size - off;
    return read_chain(file->fs, file->id, off, buf, len);
}

static int fat_readdir(const struct vnode *dir, uint32_t *cursor, struct bs_dirent *out)
{
    struct raw_entry e;
    int rc = next_entry(dir->fs, dir->id, cursor, &e);

    if (rc <= 0)
        return rc;
    display_name(&e, out->name);
    out->type = e.attr & ATTR_DIR ? BS_TYPE_DIR : BS_TYPE_FILE;
    out->size = e.size;
    return 1;
}

static const struct fs_ops fat_ops = {
    .lookup = fat_lookup,
    .read = fat_read,
    .readdir = fat_readdir,
};

int fat32_mount_device(const char *name, fat32_read_fn read, uint64_t lba)
{
    uint8_t sector[SECTOR] __attribute__((aligned(16)));
    uint16_t reserved, bytes_per_sector;
    uint32_t fat_sectors;
    struct fat *f;
    struct vnode root = {.ops = &fat_ops, .type = BS_TYPE_DIR};

    if (volume_count == MAX_VOLUMES)
        return -BS_EMFILE;
    if (read(lba, 1, sector) < 0)
        return -BS_EIO;
    f = &volumes[volume_count];

    memcpy(&bytes_per_sector, sector + 11, 2);
    memcpy(&reserved, sector + 14, 2);
    memcpy(&fat_sectors, sector + 36, 4);
    memcpy(&f->root_cluster, sector + 44, 4);
    f->sectors_per_cluster = sector[13];
    if (bytes_per_sector != SECTOR || f->sectors_per_cluster == 0 || memcmp(sector + 82, "FAT32", 5) != 0)
        return -BS_EINVAL;

    f->read = read;
    f->cached = NO_CACHE;
    f->fat_lba = lba + reserved;
    f->data_lba = f->fat_lba + (uint64_t)sector[16] * fat_sectors;
    volume_count++;
    root.fs = f;
    root.id = f->root_cluster;
    return vfs_add_volume(name, &root);
}

int fat32_mount_volume(const char *name, uint64_t lba)
{
    return fat32_mount_device(name, ahci_read, lba);
}

/* Finds a FAT32 filesystem on a whole disk: bare, MBR or first GPT partition. */
int fat32_find(fat32_read_fn read, uint64_t *lba)
{
    uint8_t sector[SECTOR] __attribute__((aligned(16)));

    if (read(0, 1, sector) < 0)
        return -1;
    if (memcmp(sector + 82, "FAT32", 5) == 0) {
        *lba = 0;
        return 0;
    }
    for (int i = 0; i < 4; i++) {
        const uint8_t *entry = sector + MBR_TABLE + i * MBR_ENTRY;
        uint32_t start;

        memcpy(&start, entry + 8, 4);
        if (entry[4] == TYPE_FAT32_CHS || entry[4] == TYPE_FAT32_LBA) {
            *lba = start;
            return 0;
        }
        if (entry[4] == TYPE_GPT) {
            if (read(GPT_ENTRIES_LBA, 1, sector) < 0)
                return -1;
            memcpy(lba, sector + 32, sizeof(*lba));
            return 0;
        }
    }
    return -1;
}
