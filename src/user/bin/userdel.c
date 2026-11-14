/* userdel name [-r]: remove an account; -r also deletes its home. Root only. */
#include "bs.h"

/* Depth-first delete. Paths are built in one buffer to keep the stack small. */
static int remove_tree(char *path, unsigned int length)
{
    struct bs_dirent entry;
    int fd = open(path, BS_O_READ), rc = 0;

    if (fd >= 0 && readdir(fd, &entry) >= 0) {
        close(fd);
        for (;;) { /* re-open each time: deleting shifts the directory under us */
            fd = open(path, BS_O_READ);
            if (fd < 0 || readdir(fd, &entry) <= 0) {
                if (fd >= 0)
                    close(fd);
                break;
            }
            close(fd);
            if (length + 1 + strlen(entry.name) >= BS_PATH_MAX)
                return -BS_ENAMETOOLONG;
            path[length] = '/';
            memcpy(path + length + 1, entry.name, strlen(entry.name) + 1);
            rc = entry.type == BS_TYPE_DIR ? remove_tree(path, length + 1 + strlen(entry.name)) : unlink(path);
            path[length] = 0;
            if (rc < 0)
                return rc;
        }
    } else if (fd >= 0) {
        close(fd);
    }
    return unlink(path);
}

int main(int argc, char **argv)
{
    char path[BS_PATH_MAX];
    struct bs_user me, target;
    int remove_home = argc == 3 && strcmp(argv[2], "-r") == 0, rc;

    if (argc < 2 || argc > 3 || (argc == 3 && !remove_home)) {
        printf("usage: userdel name [-r]\n");
        return 2;
    }
    whoami(&me);
    if (me.uid != BS_UID_ROOT) {
        printf("\033[91muserdel: root only\033[0m\n");
        return 1;
    }
    if (strcmp(argv[1], "root") == 0 || strcmp(argv[1], me.name) == 0) {
        printf("\033[91muserdel: not deleting %s out from under itself\033[0m\n", argv[1]);
        return 1;
    }
    if (user_by_name(argv[1], &target) < 0) {
        printf("\033[91muserdel: %s: no such user\033[0m\n", argv[1]);
        return 1;
    }
    table_remove("/conf/passwd", argv[1]);
    table_remove("/conf/group", argv[1]);
    table_remove("/conf/shadow", argv[1]);
    if (remove_home && strlen(target.home) > 1) {
        memcpy(path, target.home, strlen(target.home) + 1);
        rc = remove_tree(path, strlen(path));
        if (rc < 0)
            printf("\033[93muserdel: %s: %s\033[0m\n", target.home, strerror(rc));
    }
    printf("%s removed%s\n", argv[1], remove_home ? " with its home" : "");
    return 0;
}
