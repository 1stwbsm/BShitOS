/* su [user] [-c line]: a shell (or one line) as another user, root by default. */
#include "bs.h"

int main(int argc, char **argv)
{
    const char *name = "root", *line = 0;
    char password[128], *shell_argv[3];
    struct bs_user me, target;
    int code;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0 && i + 1 < argc)
            line = argv[++i];
        else if (argv[i][0] != '-')
            name = argv[i];
        else {
            printf("usage: su [user] [-c line]\n");
            return 2;
        }
    }
    if (user_by_name(name, &target) < 0) {
        printf("\033[91msu: %s: no such user\033[0m\n", name);
        return 1;
    }
    whoami(&me);
    if (me.uid != BS_UID_ROOT && read_secret("password: ", password, sizeof(password)) < 0)
        return 1;
    code = login(name, me.uid == BS_UID_ROOT ? 0 : password); /* root needs none */
    memset(password, 0, sizeof(password));
    if (code < 0) {
        printf("\033[91msu: wrong password\033[0m\n");
        return 1;
    }

    whoami(&target);
    shell_argv[0] = target.shell;
    shell_argv[1] = "-c";
    shell_argv[2] = (char *)line;
    code = spawn(target.shell, line ? 3 : 1, shell_argv);
    if (code < 0) {
        printf("\033[91msu: %s: %s\033[0m\n", target.shell, strerror(code));
        return 1;
    }
    return code;
}
