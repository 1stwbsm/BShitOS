/*
 * Accounts. /conf/passwd (world-readable) maps names to uid, gid, home and
 * shell; /conf/shadow (root only) holds password records. The kernel checks
 * passwords itself, so no program needs to read the shadow file.
 */
#include <bshit/passhash.h>

#include "drivers/pit.h"
#include "drivers/rtc.h"
#include "lib/random.h"
#include "lib/string.h"
#include "proc/proc.h"
#include "proc/users.h"

#define PASSWD_PATH "/conf/passwd"
#define SHADOW_PATH "/conf/shadow"
#define TABLE_MAX 4096u
#define FAILED_LOGIN_DELAY_MS 1000u

static char table[TABLE_MAX], rewritten[TABLE_MAX];

/* Reads a whole small file, as the kernel itself. */
static int read_table(const char *path, char *out)
{
    struct vnode node;
    int rc;

    vfs_privileged(1);
    rc = vfs_open("/", path, &node);
    if (rc == 0)
        rc = node.size < TABLE_MAX ? node.ops->read(&node, 0, out, TABLE_MAX - 1) : -BS_ENOSPC;
    vfs_privileged(0);
    if (rc < 0)
        return rc;
    out[rc] = 0;
    return rc;
}

/* Finds "name:" at the start of a line; returns the text after the colon. */
static char *find_line(char *text, const char *name)
{
    uint32_t length = strlen(name);
    char *line = text;

    while (*line) {
        if (strncmp(line, name, length) == 0 && line[length] == ':')
            return line + length + 1;
        while (*line && *line != '\n')
            line++;
        if (*line)
            line++;
    }
    return 0;
}

/* Copies the next ':' or end-of-line separated field. */
static const char *field(const char *p, char *out, uint32_t size)
{
    uint32_t n = 0;

    while (*p && *p != ':' && *p != '\n') {
        if (n + 1 < size)
            out[n++] = *p;
        p++;
    }
    out[n] = 0;
    return *p == ':' ? p + 1 : p;
}

static uint32_t number(const char *s)
{
    uint32_t v = 0;

    while (*s >= '0' && *s <= '9')
        v = v * 10 + (uint32_t)(*s++ - '0');
    return v;
}

static int valid_user_name(const char *name)
{
    uint32_t n = strlen(name);

    if (n == 0 || n >= BS_USER_NAME_MAX)
        return 0;
    for (uint32_t i = 0; i < n; i++)
        if (!((name[i] >= 'a' && name[i] <= 'z') || (name[i] >= '0' && name[i] <= '9') ||
              name[i] == '_' || name[i] == '-'))
            return 0;
    return 1;
}

static int lookup_account(const char *name, struct bs_user *out)
{
    char value[16];
    const char *p;
    int rc;

    if (!valid_user_name(name))
        return -BS_ENOENT;
    rc = read_table(PASSWD_PATH, table);
    if (rc < 0)
        return rc;
    p = find_line(table, name);
    if (!p)
        return -BS_ENOENT;
    memset(out, 0, sizeof(*out));
    strlcpy_checked(out->name, name, sizeof(out->name));
    p = field(p, value, sizeof(value));
    out->uid = number(value);
    p = field(p, value, sizeof(value));
    out->gid = number(value);
    p = field(p, out->home, sizeof(out->home));
    field(p, out->shell, sizeof(out->shell));
    if (!out->home[0])
        strlcpy_checked(out->home, "/", sizeof(out->home));
    if (!out->shell[0])
        strlcpy_checked(out->shell, "/bin/bsh.bs", sizeof(out->shell));
    return 0;
}

static int password_ok(const char *name, const char *password)
{
    char record[PASS_RECORD_MAX];
    const char *p;
    uint32_t n;

    if (read_table(SHADOW_PATH, table) < 0)
        return 0;
    p = find_line(table, name);
    if (!p)
        return 0;
    for (n = 0; p[n] && p[n] != '\n' && n + 1 < sizeof(record); n++) /* the record has colons */
        record[n] = p[n];
    record[n] = 0;
    return pass_check(password, record);
}

int users_login(const char *name, const char *password)
{
    struct proc *p = proc_current();
    struct bs_user account;
    int rc = lookup_account(name, &account);

    /* A given password is always checked, even for root (login runs as root).
     * Only root may switch without one. */
    if (rc == 0 && (password ? !password_ok(name, password) : p->user.uid != BS_UID_ROOT))
        rc = -BS_EACCES;
    if (rc < 0) {
        pit_sleep_ms(FAILED_LOGIN_DELAY_MS); /* guessing stays slow */
        return -BS_EACCES; /* the same answer for a bad name or a bad password */
    }
    p->user = account;
    return 0;
}

static int write_shadow(const char *text, uint32_t length)
{
    struct vnode node;
    int rc;

    vfs_privileged(1);
    rc = vfs_open_write("/", SHADOW_PATH, &node);
    if (rc == 0 && node.ops->setattr)
        rc = node.ops->setattr(&node, 0600, BS_UID_ROOT, 0); /* also fixes a new file */
    if (rc == 0) {
        int written = node.ops->write(&node, 0, text, length);

        rc = written == (int)length ? 0 : written < 0 ? written : -BS_EIO;
    }
    vfs_privileged(0);
    return rc;
}

int users_set_password(const char *name, const char *old_password, const char *new_password)
{
    struct proc *p = proc_current();
    struct bs_user account;
    uint8_t salt[PASS_SALT_BYTES];
    char record[PASS_RECORD_MAX];
    uint32_t used = 0;
    const char *line;
    int rc = lookup_account(name, &account), replaced = 0;

    if (rc < 0)
        return rc;
    if (p->user.uid != BS_UID_ROOT) {
        if (strcmp(p->user.name, name) != 0)
            return -BS_EPERM;
        if (!password_ok(name, old_password)) {
            pit_sleep_ms(FAILED_LOGIN_DELAY_MS);
            return -BS_EACCES;
        }
    }
    random_bytes(salt, sizeof(salt));
    if (pass_make_record(new_password, salt, record, sizeof(record)) < 0)
        return -BS_EINVAL;

    /* Copy every other line; replace or append this one. */
    if (read_table(SHADOW_PATH, table) < 0)
        table[0] = 0;
    line = table;
    while (*line) {
        const char *end = line;
        uint32_t length, name_length = strlen(name);

        while (*end && *end != '\n')
            end++;
        length = (uint32_t)(end - line);
        if (length > name_length && strncmp(line, name, name_length) == 0 && line[name_length] == ':') {
            replaced = 1; /* this line gets the new record */
            if (used + name_length + strlen(record) + 2 >= TABLE_MAX)
                return -BS_ENOSPC;
            memcpy(rewritten + used, name, name_length);
            used += name_length;
            rewritten[used++] = ':';
            memcpy(rewritten + used, record, strlen(record));
            used += strlen(record);
            rewritten[used++] = '\n';
        } else if (length) {
            if (used + length + 1 >= TABLE_MAX)
                return -BS_ENOSPC;
            memcpy(rewritten + used, line, length);
            used += length;
            rewritten[used++] = '\n';
        }
        line = *end ? end + 1 : end;
    }
    if (!replaced) {
        uint32_t name_length = strlen(name), record_length = strlen(record);

        if (used + name_length + record_length + 2 >= TABLE_MAX)
            return -BS_ENOSPC;
        memcpy(rewritten + used, name, name_length);
        used += name_length;
        rewritten[used++] = ':';
        memcpy(rewritten + used, record, record_length);
        used += record_length;
        rewritten[used++] = '\n';
    }
    return write_shadow(rewritten, used);
}
