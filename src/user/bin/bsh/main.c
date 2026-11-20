/*
 * bsh: the BShitOS shell.
 *
 *   bsh                 interactive
 *   bsh script [args]   run a script
 *   bsh -c 'line'       run one line
 */
#include "bsh.h"

#define CONF_PATH "/conf/bsh.conf"
#define MOTD_PATH "/conf/motd"
#define SYSTEM_RC "/conf/bshrc"
#define AUTOEXEC "/conf/autoexec.bsh"
#define DEFAULT_PROMPT "%u %{93}%d%{0}%?%t %{1}>>%{0} "

static unsigned int last_took;

static struct bs_user me;

static void defaults(void)
{
    char value[256];

    whoami(&me);
    var_set("PATH", "/bin");
    var_set("HOME", me.home);
    var_set("USER", me.name);
    var_set("PROMPT", conf_get(CONF_PATH, "prompt", value, sizeof(value)) == 0 ? value : DEFAULT_PROMPT);
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

static void run_rc(const char *path)
{
    char *argv[] = {(char *)path};
    int fd = open(path, BS_O_READ);

    if (fd < 0)
        return; /* rc files are optional */
    close(fd);
    run_script(path, 1, argv);
}

/*
 * $PROMPT codes: %u user (red when root), %d directory (~ for $HOME), %? " ?N"
 * when the last command failed, %t " Ns" when it took 2 s or more,
 * %{codes} an SGR colour, %% a %.
 */
static void build_prompt(char *out, unsigned int size)
{
    const char *format = var_get("PROMPT"), *home = var_get("HOME");
    char cwd[BS_PATH_MAX], number[12], code[24];

    out[0] = 0;
    if (!format)
        format = DEFAULT_PROMPT;
    getcwd(cwd, sizeof(cwd));
    for (const char *p = format; *p; p++) {
        char literal[2] = {*p, 0};

        if (*p != '%' || !p[1]) {
            append(out, size, literal);
            continue;
        }
        p++;
        if (*p == 'd') {
            unsigned int home_length = home ? strlen(home) : 0;

            if (home_length > 1 && starts_with(cwd, home) && (cwd[home_length] == 0 || cwd[home_length] == '/')) {
                append(out, size, "~");
                append(out, size, cwd + home_length);
            } else {
                append(out, size, cwd);
            }
        } else if (*p == 'u') {
            append(out, size, me.uid == BS_UID_ROOT ? C_ERROR : C_VAR);
            append(out, size, me.name);
            append(out, size, C_RESET);
        } else if (*p == '?') {
            if (last_status) {
                format_uint((unsigned int)last_status, number);
                append(out, size, C_ERROR " ?");
                append(out, size, number);
                append(out, size, C_RESET);
            }
        } else if (*p == 't') {
            if (last_took >= 2) {
                append(out, size, C_DIM " ");
                if (last_took >= 60) {
                    format_uint(last_took / 60, number);
                    append(out, size, number);
                    append(out, size, "m");
                }
                format_uint(last_took % 60, number);
                append(out, size, number);
                append(out, size, "s" C_RESET);
            }
        } else if (*p == '{') {
            const char *end = strchr(p, '}');
            unsigned int length = end ? (unsigned int)(end - p - 1) : 0;

            if (!end || length > 16) {
                append(out, size, "%{");
                continue;
            }
            code[0] = 0;
            append(code, sizeof(code), "\033[");
            memcpy(code + 2, p + 1, length);
            code[2 + length] = 'm';
            code[3 + length] = 0;
            append(out, size, code);
            p = end;
        } else {
            literal[0] = *p;
            append(out, size, literal);
        }
    }
}

/*
 * !! anywhere (outside single quotes) is the last line. A line starting with
 * !n or !prefix is that history entry. Returns -1 if there is no such entry.
 */
static int expand_history(const char *in, char *out, unsigned int size)
{
    const char *last = history_entry(history_count() - 1), *p = in;
    int changed = 0, quoted = 0;

    out[0] = 0;
    if (in[0] == '!' && in[1] && in[1] != '!' && in[1] != ' ' && in[1] != '=') {
        char event[64];
        const char *entry = 0;
        unsigned int n = 0;

        while (in[1 + n] && in[1 + n] != ' ' && n < sizeof(event) - 1) {
            event[n] = in[1 + n];
            n++;
        }
        event[n] = 0;
        if (event[0] >= '0' && event[0] <= '9') {
            entry = history_entry(atoi(event) - 1);
        } else {
            for (int i = history_count() - 1; i >= 0 && !entry; i--)
                if (starts_with(history_entry(i), event))
                    entry = history_entry(i);
        }
        if (!entry) {
            bsh_error(event, "no such line in history");
            return -1;
        }
        append(out, size, entry);
        p = in + 1 + n;
        changed = 1;
    }
    for (; *p; p++) {
        char literal[2] = {*p, 0};

        if (*p == '\'')
            quoted = !quoted;
        if (!quoted && p[0] == '!' && p[1] == '!') {
            if (!last) {
                bsh_error("!!", "no previous line");
                return -1;
            }
            append(out, size, last);
            p++;
            changed = 1;
        } else {
            append(out, size, literal);
        }
    }
    return changed;
}

int main(int argc, char **argv)
{
    static char line[LINE_MAX], expanded[LINE_MAX], prompt[256], cwd[BS_PATH_MAX];

    defaults();
    if (argc > 2 && strcmp(argv[1], "-c") == 0)
        return run_line(argv[2]);
    if (argc > 1)
        return run_script(argv[1], argc - 1, argv + 1);

    if (argv[0][0] == '-') /* login shell: greet, start at home */
        show_motd();
    chdir(var_get("HOME"));
    getcwd(cwd, sizeof(cwd));
    var_set("PWD", cwd);
    run_rc(SYSTEM_RC);
    if (var_get("HOME")) {
        char user_rc[BS_PATH_MAX];

        user_rc[0] = 0;
        if (append(user_rc, sizeof(user_rc), var_get("HOME")) == 0 && append(user_rc, sizeof(user_rc), "/.bshrc") == 0)
            run_rc(user_rc);
    }
    last_status = 0;
    history_load();
    /* A login shell runs autoexec, which starts BDeskUI. An exit in it ends the session. */
    if (argv[0][0] == '-') {
        run_rc(AUTOEXEC);
        if (run_script_exited())
            return last_status;
        last_status = 0;
    }

    while (!exit_requested() && !shutting_down()) {
        unsigned int started;
        int changed;

        commands_refresh();
        build_prompt(prompt, sizeof(prompt));
        if (edit_line(prompt, line, sizeof(line)) <= 0)
            continue;
        changed = expand_history(line, expanded, sizeof(expanded));
        if (changed < 0) {
            last_status = 1;
            continue;
        }
        if (changed)
            printf(C_DIM "%s" C_RESET "\n", expanded);
        history_add(expanded);

        started = uptime();
        run_line(expanded);
        last_took = uptime() - started;
    }
    return last_status;
}
