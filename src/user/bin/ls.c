/* ls [path...]: lists directories; files are listed by name. */
#include "bs.h"

static int list(const char *path, int heading)
{
    struct bs_dirent entry;
    int fd = open(path, BS_O_READ), rc;

    if (fd < 0) {
        printf("ls: %s: %s\n", path, strerror(fd));
        return 1;
    }
    rc = readdir(fd, &entry);
    if (rc < 0) { /* not a directory: the path itself is the listing */
        close(fd);
        printf("%s\n", path);
        return 0;
    }
    if (heading)
        printf("\033[1m%s:\033[0m\n", path);
    for (; rc > 0; rc = readdir(fd, &entry)) {
        if (entry.type == BS_TYPE_DIR)
            printf("\033[94m%s/\033[0m\n", entry.name);
        else if (entry.type == BS_TYPE_SYMLINK)
            printf("\033[96m%s@\033[0m\n", entry.name);
        else
            printf("%s  \033[90m%u bytes\033[0m\n", entry.name, entry.size);
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
    int status = 0;

    if (argc < 2)
        return list(".", 0);
    for (int i = 1; i < argc; i++)
        status |= list(argv[i], argc > 2);
    return status;
}
