/* Read-only ISO9660 backend for CDs and DVDs. */
#include "drivers/ahci.h"
#include "fs/partitions.h"
#include "fs/vfs.h"
#include "lib/string.h"

#define SECTOR AHCI_CD_SECTOR_SIZE
#define PVD_LBA 16
#define PVD_ROOT_RECORD 156
#define FLAG_DIRECTORY 0x02
#define RECORD_MIN 34

static const struct fs_ops iso_ops;

struct record {
    uint32_t extent, size, flags;
    char name[BS_NAME_MAX];
};

static uint8_t sector_cache[SECTOR] __attribute__((aligned(16)));
static uint32_t cached_lba = 0xFFFFFFFFu;

static int read_sector(uint32_t lba)
{
    if (lba == cached_lba)
        return 0;
    if (ahci_cd_read(lba, 1, sector_cache) < 0) {
        cached_lba = 0xFFFFFFFFu;
        return -BS_EIO;
    }
    cached_lba = lba;
    return 0;
}

static void parse_record(const uint8_t *r, struct record *out)
{
    uint32_t length = r[32], n = 0;

    memcpy(&out->extent, r + 2, 4);
    memcpy(&out->size, r + 10, 4);
    out->flags = r[25];
    for (uint32_t i = 0; i < length && n + 1 < BS_NAME_MAX && r[33 + i] != ';'; i++)
        out->name[n++] = (char)r[33 + i];
    if (n && out->name[n - 1] == '.')
        n--;
    out->name[n] = 0;
}

/* Returns 1 with the next entry, 0 at the end. *cursor is a byte offset. */
static int next_record(const struct vnode *dir, uint32_t *cursor, struct record *out)
{
    while (*cursor < dir->size) {
        uint32_t lba = dir->id + *cursor / SECTOR, within = *cursor % SECTOR;
        const uint8_t *r;
        int rc = read_sector(lba);

        if (rc < 0)
            return rc;
        r = sector_cache + within;
        if (within + RECORD_MIN > SECTOR || r[0] == 0) {
            *cursor += SECTOR - within; /* padding to the next sector */
            continue;
        }
        *cursor += r[0];
        if (r[32] == 1 && r[33] <= 1)
            continue; /* the . and .. records */
        parse_record(r, out);
        return 1;
    }
    return 0;
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

static int iso_lookup(const struct vnode *dir, const char *name, struct vnode *out)
{
    struct record entry;
    uint32_t cursor = 0;
    int rc;

    while ((rc = next_record(dir, &cursor, &entry)) > 0) {
        if (names_match(entry.name, name)) {
            out->ops = &iso_ops;
            out->fs = 0;
            out->id = entry.extent;
            out->type = entry.flags & FLAG_DIRECTORY ? BS_TYPE_DIR : BS_TYPE_FILE;
            out->size = entry.size;
            out->owned = 0; /* no owners or permission bits here */
            return 0;
        }
    }
    return rc < 0 ? rc : -BS_ENOENT;
}

static int iso_read(const struct vnode *file, uint32_t off, void *buf, uint32_t len)
{
    uint8_t *dst = buf;
    uint32_t done = 0;

    if (off >= file->size)
        return 0;
    if (len > file->size - off)
        len = file->size - off;

    while (done < len) {
        uint32_t position = off + done, within = position % SECTOR;
        uint32_t chunk = SECTOR - within;
        int rc = read_sector(file->id + position / SECTOR);

        if (rc < 0)
            return rc;
        if (chunk > len - done)
            chunk = len - done;
        memcpy(dst + done, sector_cache + within, chunk);
        done += chunk;
    }
    return (int)done;
}

static int iso_readdir(const struct vnode *dir, uint32_t *cursor, struct bs_dirent *out)
{
    struct record entry;
    int rc = next_record(dir, cursor, &entry);

    if (rc <= 0)
        return rc;
    memcpy(out->name, entry.name, BS_NAME_MAX);
    out->type = entry.flags & FLAG_DIRECTORY ? BS_TYPE_DIR : BS_TYPE_FILE;
    out->size = entry.size;
    return 1;
}

static const struct fs_ops iso_ops = {
    .lookup = iso_lookup,
    .read = iso_read,
    .readdir = iso_readdir,
};

int iso9660_mount_volume(const char *name)
{
    struct record root_record;
    struct vnode root = {.ops = &iso_ops, .type = BS_TYPE_DIR};
    int rc = read_sector(PVD_LBA);

    if (rc < 0)
        return rc;
    if (memcmp(sector_cache + 1, "CD001", 5) != 0)
        return -BS_EINVAL;
    parse_record(sector_cache + PVD_ROOT_RECORD, &root_record);
    root.id = root_record.extent;
    root.size = root_record.size;
    return vfs_add_volume(name, &root);
}
