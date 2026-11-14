/* ls [-l] [path...]: lists directories; files are listed by name. -l adds mode, owners and size. */
#include "bs.h"

static int long_format;

static void pad(const char *s, unsigned int width)
{
    unsigned int n = strlen(s);

    printf("%s", s);
    while (n++ < width)
        printf(" ");
}

static void pad_number(unsigned int value, unsigned int width)
{
    char digits[12];
    unsigned int n = 0;

    do {
        digits[n++] = (char)('0' + value % 10);
        value /= 10;
    } while (value);
    while (width-- > n)
        printf(" ");
    while (n)
        printf("%c", digits[--n]);
}

static void show(const char *path, const char *name, unsigned int type, unsigned int size)
{
    if (long_format) {
        static const char kinds[] = "?-dl";
        char mode[11], owner[BS_USER_NAME_MAX], group[BS_USER_NAME_MAX];
        struct bs_stat st;

        if (stat(path, &st) < 0) {
            st.perm = 0;
            st.uid = st.gid = 0;
        }
        mode[0] = kinds[type < 4 ? type : 0];
        for (int i = 0; i < 9; i++)
            mode[1 + i] = st.perm & (0400u >> i) ? "rwx"[i % 3] : '-';
        mode[10] = 0;
        user_name(st.uid, owner, sizeof(owner));
        group_name(st.gid, group, sizeof(group));
        printf("\033[90m%s\033[0m ", mode);
        pad(owner, 9);
        pad(group, 9);
        printf("\033[90m");
        pad_number(size, 9);
        printf("\033[0m ");
    }
    if (type == BS_TYPE_DIR)
        printf("\033[94m%s/\033[0m\n", name);
    else if (type == BS_TYPE_SYMLINK)
        printf("\033[96m%s@\033[0m\n", name);
    else if (long_format)
        printf("%s\n", name);
    else
        printf("%s  \033[90m%u bytes\033[0m\n", name, size);
}

static int list(const char *path, int heading)
{
    char child[BS_PATH_MAX];
    struct bs_dirent entry;
    int fd = open(path, BS_O_READ), rc;

    if (fd < 0) {
        printf("ls: %s: %s\n", path, strerror(fd));
        return 1;
    }
    rc = readdir(fd, &entry);
    if (rc < 0) { /* not a directory: the path itself is the listing */
        struct bs_stat st;

        close(fd);
        if (stat(path, &st) < 0)
            st.size = 0;
        show(path, path, BS_TYPE_FILE, st.size);
        return 0;
    }
    if (heading)
        printf("\033[1m%s:\033[0m\n", path);
    for (; rc > 0; rc = readdir(fd, &entry)) {
        unsigned int length = strlen(path);

        if (length + strlen(entry.name) + 2 < sizeof(child)) {
            memcpy(child, path, length);
            child[length] = '/';
            memcpy(child + length + 1, entry.name, strlen(entry.name) + 1);
        } else {
            child[0] = 0;
        }
        show(child, entry.name, entry.type, entry.size);
    }
    close(fd);
    if (rc < 0) {
        printf("ls: %s: %s\n", path, strerror(rc));
        return 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    int status = 0, paths = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-l") == 0)
            long_format = 1;
        else
            paths++;
    }
    if (paths == 0)
        return list(".", 0);
    for (int i = 1; i < argc; i++)
        if (strcmp(argv[i], "-l") != 0)
            status |= list(argv[i], paths > 1);
    return status;
}
