/* chown user[:group] path...: change owners. Root only. */
#include "bs.h"

int main(int argc, char **argv)
{
    char owner[BS_USER_NAME_MAX * 2], *group;
    struct bs_user user;
    unsigned int gid;
    int status = 0;

    if (argc < 3 || strlen(argv[1]) >= sizeof(owner)) {
        printf("usage: chown user[:group] path...\n");
        return 2;
    }
    memcpy(owner, argv[1], strlen(argv[1]) + 1);
    group = strchr(owner, ':');
    if (group)
        *group++ = 0;
    if (user_by_name(owner, &user) < 0) {
        printf("\033[91mchown: %s: no such user\033[0m\n", owner);
        return 1;
    }
    gid = user.gid;
    if (group && *group && group_by_name(group, &gid) < 0) {
        printf("\033[91mchown: %s: no such group\033[0m\n", group);
        return 1;
    }
    for (int i = 2; i < argc; i++) {
        int rc = chown(argv[i], user.uid, gid);

        if (rc < 0) {
            printf("\033[91mchown: %s: %s\033[0m\n", argv[i], strerror(rc));
            status = 1;
        }
    }
    return status;
}
