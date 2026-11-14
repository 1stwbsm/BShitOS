/* bsh: the BShitOS shell. */
#include "bs.h"

#define LINE_MAX 256
#define ARGS_MAX 16
#define PROMPT_MAX 32
#define BIN_DIR "/bin/"
#define EXT ".bs"
#define CONF_PATH "/conf/bsh.conf"
#define MOTD_PATH "/conf/motd"

static char prompt[PROMPT_MAX] = "bsh";
static char home[BS_PATH_MAX] = "/userspace/user";

static void load_config(void)
{
    char value[PROMPT_MAX];

    if (conf_get(CONF_PATH, "prompt", value, sizeof(value)) == 0)
        memcpy(prompt, value, strlen(value) + 1);
    conf_get(CONF_PATH, "home", home, sizeof(home));
}

static void show_motd(void)
{
    char data[512];
    int fd = open(MOTD_PATH, BS_O_READ), got;

    if (fd < 0)
        return;
    while ((got = read(fd, data, sizeof(data))) > 0)
        write(1, data, (unsigned int)got);
    close(fd);
}

static int split(char *line, char **argv)
{
    int argc = 0;

    while (*line && argc < ARGS_MAX) {
        while (*line == ' ' || *line == '\n' || *line == '\t')
            *line++ = 0;
        if (!*line)
            break;
        argv[argc++] = line;
        while (*line && *line != ' ' && *line != '\n' && *line != '\t')
            line++;
    }
    if (*line)
        *line = 0;
    return argc;
}

static void run_program(int argc, char **argv)
{
    char path[BS_PATH_MAX];
    int code;

    if (strchr(argv[0], '/')) {
        if (strlen(argv[0]) >= sizeof(path)) {
            printf("bsh: command too long\n");
            return;
        }
        memcpy(path, argv[0], strlen(argv[0]) + 1);
    } else {
        unsigned int length = strlen(argv[0]);

        if (length + sizeof(BIN_DIR) + sizeof(EXT) > sizeof(path)) {
            printf("bsh: command too long\n");
            return;
        }
        memcpy(path, BIN_DIR, sizeof(BIN_DIR) - 1);
        memcpy(path + sizeof(BIN_DIR) - 1, argv[0], length);
        memcpy(path + sizeof(BIN_DIR) - 1 + length, EXT, sizeof(EXT));
    }

    code = spawn(path, argc, argv);
    if (code == -BS_ENOENT)
        printf("\033[91mbsh: %s: command not found\033[0m\n", argv[0]);
    else if (code < 0)
        printf("\033[91mbsh: %s: %s\033[0m\n", argv[0], strerror(code));
}

static void show_help(void)
{
    printf("\033[1mbuilt in:\033[0m cd [dir], pwd, help, exit\n");
    printf("\033[1mfiles:\033[0m   ls [dir], cat file, edit file, cp a b, mv a b, rm path,\n");
    printf("         mkdir dir, write file text\n");
    printf("\033[1mdesk:\033[0m    desk (the windowed environment)\n");
    printf("\033[1mfun:\033[0m     cube, banner text, beep [hz] [ms], tune\n");
    printf("\033[1mweb:\033[0m     web [url] (press p for mouse mode), net, ping host\n");
    printf("\033[1msystem:\033[0m  clear, date, echo text, serial, noserial, reboot, poweroff\n");
    printf("\033[1mediting:\033[0m left/right, home/end, del, up/down for history\n");
}

int main(void)
{
    char line[LINE_MAX], cwd[BS_PATH_MAX];
    char *argv[ARGS_MAX];

    load_config();
    show_motd();
    chdir(home);

    for (;;) {
        int got, argc;

        getcwd(cwd, sizeof(cwd));
        printf("\033[96m%s\033[0m:\033[93m%s\033[0m$ ", prompt, cwd);
        got = read(0, line, sizeof(line) - 1);
        if (got <= 0)
            continue;
        line[got] = 0;
        argc = split(line, argv);
        if (argc == 0)
            continue;

        if (strcmp(argv[0], "exit") == 0) {
            return 0;
        } else if (strcmp(argv[0], "help") == 0) {
            show_help();
        } else if (strcmp(argv[0], "pwd") == 0) {
            printf("%s\n", cwd);
        } else if (strcmp(argv[0], "cd") == 0) {
            int rc = chdir(argc > 1 ? argv[1] : home);

            if (rc < 0)
                printf("cd: %s\n", strerror(rc));
        } else {
            run_program(argc, argv);
        }
    }
}
