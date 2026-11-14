/* Looking up accounts in /conf/passwd and /conf/group, and reading passwords. */
#include "bs.h"

#define PASSWD_PATH "/conf/passwd"
#define GROUP_PATH "/conf/group"
#define TABLE_MAX 4096

static char table[TABLE_MAX];

static int load(const char *path)
{
    int fd = open(path, BS_O_READ), got, total = 0;

    if (fd < 0)
        return fd;
    while (total < TABLE_MAX - 1 && (got = read(fd, table + total, (unsigned int)(TABLE_MAX - 1 - total))) > 0)
        total += got;
    close(fd);
    table[total] = 0;
    return 0;
}

/* Splits the line at p into up to max ':' fields, in place. Returns the next line. */
static char *fields(char *p, char **out, int max)
{
    int n = 0;

    out[n++] = p;
    while (*p && *p != '\n') {
        if (*p == ':' && n < max) {
            *p = 0;
            out[n++] = p + 1;
        }
        p++;
    }
    if (*p)
        *p++ = 0;
    while (n < max)
        out[n++] = "";
    return p;
}

static void copy(char *dst, const char *src, unsigned int size)
{
    unsigned int n = strlen(src);

    if (n >= size)
        n = size - 1;
    memcpy(dst, src, n);
    dst[n] = 0;
}

int user_by_name(const char *name, struct bs_user *out)
{
    char *p, *f[5];

    if (load(PASSWD_PATH) < 0)
        return -BS_ENOENT;
    for (p = table; *p;) {
        p = fields(p, f, 5);
        if (f[0][0] == '#' || strcmp(f[0], name) != 0)
            continue;
        memset(out, 0, sizeof(*out));
        copy(out->name, f[0], sizeof(out->name));
        out->uid = (unsigned int)atoi(f[1]);
        out->gid = (unsigned int)atoi(f[2]);
        copy(out->home, f[3], sizeof(out->home));
        copy(out->shell, f[4], sizeof(out->shell));
        return 0;
    }
    return -BS_ENOENT;
}

/* Name for an id in a passwd- or group-style table (id in the given column). */
static int name_for(const char *path, int column, unsigned int id, char *out, unsigned int size)
{
    char *p, *f[3];

    if (load(path) == 0) {
        for (p = table; *p;) {
            p = fields(p, f, 3);
            if (f[0][0] && f[0][0] != '#' && (unsigned int)atoi(f[column]) == id && f[column][0]) {
                copy(out, f[0], size);
                return 0;
            }
        }
    }
    /* unknown: the number itself */
    {
        char digits[12];
        int n = 0;

        do {
            digits[n++] = (char)('0' + id % 10);
            id /= 10;
        } while (id);
        if ((unsigned int)n >= size)
            n = (int)size - 1;
        for (int i = 0; i < n; i++)
            out[i] = digits[n - 1 - i];
        out[n] = 0;
    }
    return -BS_ENOENT;
}

int user_name(unsigned int uid, char *out, unsigned int size)
{
    return name_for(PASSWD_PATH, 1, uid, out, size);
}

int group_name(unsigned int gid, char *out, unsigned int size)
{
    return name_for(GROUP_PATH, 1, gid, out, size);
}

int group_by_name(const char *name, unsigned int *gid)
{
    char *p, *f[3];

    if (load(GROUP_PATH) < 0)
        return -BS_ENOENT;
    for (p = table; *p;) {
        p = fields(p, f, 3);
        if (f[0][0] != '#' && strcmp(f[0], name) == 0) {
            *gid = (unsigned int)atoi(f[1]);
            return 0;
        }
    }
    return -BS_ENOENT;
}

int read_secret(const char *prompt, char *out, unsigned int size)
{
    unsigned int length = 0;

    printf("%s", prompt);
    for (;;) {
        int key = poll_key();

        if (key == '\n') {
            out[length] = 0;
            printf("\n");
            return (int)length;
        }
        if (key == 3) { /* Ctrl+C */
            memset(out, 0, size);
            printf("^C\n");
            return -1;
        }
        if (key == '\b' && length > 0)
            length--;
        else if (key == 21) /* Ctrl+U */
            length = 0;
        else if (key >= 0x20 && key < 0x7F && length + 1 < size)
            out[length++] = (char)key;
    }
}

static int save(const char *path, const char *text, unsigned int length)
{
    int fd = open(path, BS_O_WRITE), written;

    if (fd < 0)
        return fd;
    written = write(fd, text, length);
    close(fd);
    return written == (int)length ? 0 : written < 0 ? written : -BS_EIO;
}

int table_append(const char *path, const char *line)
{
    unsigned int used, length = strlen(line);

    if (load(path) < 0)
        table[0] = 0;
    used = strlen(table);
    if (used && table[used - 1] != '\n')
        table[used++] = '\n';
    if (used + length + 2 >= TABLE_MAX)
        return -BS_ENOSPC;
    memcpy(table + used, line, length);
    used += length;
    table[used++] = '\n';
    return save(path, table, used);
}

int table_remove(const char *path, const char *name)
{
    static char kept[TABLE_MAX];
    unsigned int used = 0, name_length = strlen(name);
    int removed = 0;

    if (load(path) < 0)
        return -BS_ENOENT;
    for (char *line = table; *line;) {
        char *end = line;
        unsigned int length;

        while (*end && *end != '\n')
            end++;
        length = (unsigned int)(end - line);
        if (length > name_length && strncmp(line, name, name_length) == 0 && line[name_length] == ':') {
            removed = 1;
        } else if (length) {
            memcpy(kept + used, line, length);
            used += length;
            kept[used++] = '\n';
        }
        line = *end ? end + 1 : end;
    }
    if (!removed)
        return -BS_ENOENT;
    return save(path, kept, used);
}
