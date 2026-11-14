#include "bs.h"

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : ".";
    struct bs_dirent entry;
    int fd = open(path, BS_O_READ), rc;

    if (fd < 0) {
        printf("ls: %s: %s\n", path, strerror(fd));
        return 1;
    }
    while ((rc = readdir(fd, &entry)) > 0) {
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
