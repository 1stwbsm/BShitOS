/* passwd [user]: change a password. Root can change anyone's without the old one. */
#include "bs.h"

int main(int argc, char **argv)
{
    char current[128], fresh[128], again[128];
    struct bs_user me;
    const char *name;
    int rc;

    whoami(&me);
    name = argc > 1 ? argv[1] : me.name;
    if (me.uid != BS_UID_ROOT && strcmp(name, me.name) != 0) {
        printf("\033[91mpasswd: only root can change someone else's password\033[0m\n");
        return 1;
    }
    current[0] = 0;
    if (me.uid != BS_UID_ROOT && read_secret("current password: ", current, sizeof(current)) < 0)
        return 1;
    if (read_secret("new password: ", fresh, sizeof(fresh)) < 0 || read_secret("again: ", again, sizeof(again)) < 0)
        return 1;
    if (strcmp(fresh, again) != 0) {
        printf("\033[91mpasswd: they don't match, nothing changed\033[0m\n");
        return 1;
    }
    if (fresh[0] == 0)
        printf("\033[93mpasswd: empty password: anyone at the keyboard gets in as %s\033[0m\n", name);
    rc = set_password(name, current, fresh);
    memset(current, 0, sizeof(current));
    memset(fresh, 0, sizeof(fresh));
    memset(again, 0, sizeof(again));
    if (rc < 0) {
        printf("\033[91mpasswd: %s\033[0m\n", rc == -BS_EACCES ? "wrong current password" : strerror(rc));
        return 1;
    }
    printf("password for %s changed\n", name);
    return 0;
}
