/*
 * login: the first program. Runs as root, asks who you are, switches to that
 * account and starts its shell. When the shell exits, login exits too and the
 * kernel starts it again.
 *
 * Booted live, it logs straight in as live_user from /conf/login.conf.
 */
#include "bs.h"

#define CONF_PATH "/conf/login.conf"
#define TRIES_BEFORE_PAUSE 3

static int read_name(char *out, unsigned int size)
{
    unsigned int length = 0;

    printf("\033[93mbshitos\033[0m login: ");
    for (unsigned int idle = 1;; idle++) {
        int key = poll_key();

        if (!key && (idle & 63) == 0 && shutting_down())
            return -1;

        if (key == '\n') {
            out[length] = 0;
            printf("\n");
            return (int)length;
        }
        if (key == '\b' && length > 0) {
            length--;
            printf("\b");
        } else if (((key >= 'a' && key <= 'z') || (key >= '0' && key <= '9') || key == '_' || key == '-') &&
                   length + 1 < size) {
            out[length++] = (char)key;
            printf("%c", key);
        }
    }
}

static int start_session(void)
{
    struct bs_user me;
    char *argv[1] = {"-bsh"}; /* a leading - marks a login shell */
    int code;

    whoami(&me);
    if (chdir(me.home) < 0) {
        printf("\033[93mlogin: home %s is missing, starting in /\033[0m\n", me.home);
        chdir("/");
    }
    code = spawn(me.shell, 1, argv);
    if (code < 0)
        printf("\033[91mlogin: %s: %s\033[0m\n", me.shell, strerror(code));
    return code < 0 ? 1 : 0;
}

#define EXIT_TTY_LOGIN 4 /* bdeskui: the login screen's TTY button */

/*
 * Where there is a graphics mode, BDeskUI's login screen does the asking (it runs
 * as root until someone logs in, then becomes their session). Returns 1 if it
 * handled the login and the session has ended, 0 to ask on the text console.
 */
static int graphical_login(const struct bs_sysinfo *info, const char *live_user)
{
    char value[8], *argv[4] = {"bdeskui", "--login", "--auto", (char *)live_user};
    int code;

    if (!info->screen_width || (conf_get(CONF_PATH, "graphical", value, sizeof(value)) == 0 &&
                                strcmp(value, "no") == 0))
        return 0;
    code = spawn("/bin/bdeskui.bs", live_user ? 4 : 2, argv);
    return code >= 0 && code != EXIT_TTY_LOGIN;
}

int main(void)
{
    char name[BS_USER_NAME_MAX], password[128], live_user[BS_USER_NAME_MAX];
    struct bs_sysinfo info;
    int failures = 0, live;

    sysinfo(&info);
    live = (info.flags & BS_SYSINFO_LIVE) && conf_get(CONF_PATH, "live_user", live_user, sizeof(live_user)) == 0;
    if (graphical_login(&info, live ? live_user : 0))
        return 0; /* the session is over; the kernel starts login again */
    if (live && login(live_user, 0) == 0) {
        printf("\033[90mlive session: logged in as %s. root's password is empty: 'su -c install' installs to disk.\033[0m\n",
               live_user);
        return start_session();
    }

    while (!shutting_down()) {
        int rc;

        if (read_name(name, sizeof(name)) <= 0)
            continue;
        if (read_secret("password: ", password, sizeof(password)) < 0)
            continue;
        rc = login(name, password);
        memset(password, 0, sizeof(password));
        if (rc == 0) {
            printf("\n");
            return start_session();
        }
        printf("\033[91mwrong name or password\033[0m\n");
        if (++failures % TRIES_BEFORE_PAUSE == 0)
            sleep_ms(3000); /* the kernel already waits a second per try */
    }
    return 0; /* shutting down */
}
