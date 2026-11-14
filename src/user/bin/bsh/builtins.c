/* Commands bsh runs itself, because they change the shell's own state. */
#include "bsh.h"

static void columns(const char **names, const char *color, int count)
{
    int width = 0, screen_cols, screen_rows, per_row, rows;

    console_size(&screen_cols, &screen_rows);
    for (int i = 0; i < count; i++)
        if ((int)strlen(names[i]) > width)
            width = (int)strlen(names[i]);
    width += 2;
    per_row = (screen_cols - 2) / width;
    if (per_row < 1)
        per_row = 1;
    rows = (count + per_row - 1) / per_row;
    for (int r = 0; r < rows; r++) {
        put("  ");
        for (int c = 0; c < per_row; c++) {
            int i = c * rows + r;

            if (i >= count)
                continue;
            put(color);
            put_padded(names[i], c + 1 < per_row && i + rows < count ? (unsigned int)width : 0);
            put(C_RESET);
        }
        put("\n");
    }
}

static int help(int argc, char **argv)
{
    const char *programs[NAMES_MAX];
    int count = 0;

    (void)argc;
    (void)argv;
    printf(C_BOLD "builtins" C_RESET "\n");
    for (int i = 0; builtins[i].name; i++) {
        put("  " C_BUILTIN);
        put_padded(builtins[i].usage, 22);
        printf(C_RESET C_DIM "%s" C_RESET "\n", builtins[i].about);
    }

    for (int i = 0; i < command_count() && count < NAMES_MAX; i++)
        if (command_kind(command_name(i)) == KIND_PROGRAM && !builtin_find(command_name(i)))
            programs[count++] = command_name(i);
    for (int i = 1; i < count; i++)
        for (int j = i; j > 0 && strcmp(programs[j - 1], programs[j]) > 0; j--) {
            const char *t = programs[j];

            programs[j] = programs[j - 1];
            programs[j - 1] = t;
        }
    printf(C_BOLD "programs" C_RESET C_DIM " ($PATH = %s)" C_RESET "\n", var_get("PATH") ? var_get("PATH") : "");
    columns(programs, C_PROGRAM, count);

    printf(C_BOLD "syntax" C_RESET "\n"
           "  a ; b   a && b   a || b   # comment     'raw'   \"with $VAR\"   \\x\n"
           "  NAME=value   $NAME ${NAME}   $? last status   $1..$9 $# script args\n"
           "  ~ home   *.txt  file?.c globs   !! last line   !n  !prefix   ' cmd' skips history\n");
    printf(C_BOLD "keys" C_RESET "\n"
           "  Tab complete   Up/Down history, filtered by what you typed   Ctrl+R search\n"
           "  Ctrl+A/E start/end   Ctrl+U/K/W cut   Ctrl+Y paste   Ctrl+L clear   Ctrl+C drop\n");
    printf(C_BOLD "files" C_RESET "\n"
           "  /conf/bshrc and ~/.bshrc run at start   ~/.bsh_history   name.bsh = script\n");
    return 0;
}

static int cd(int argc, char **argv)
{
    char before[BS_PATH_MAX], after[BS_PATH_MAX];
    const char *target = argc > 1 ? argv[1] : var_get("HOME");
    int rc;

    if (argc > 1 && strcmp(argv[1], "-") == 0) {
        target = var_get("OLDPWD");
        if (!target)
            return bsh_error("cd", "no previous directory yet");
    }
    getcwd(before, sizeof(before));
    rc = chdir(target ? target : "/");
    if (rc < 0)
        return bsh_error(target ? target : "/", strerror(rc));
    getcwd(after, sizeof(after));
    var_set("OLDPWD", before);
    var_set("PWD", after);
    if (argc > 1 && strcmp(argv[1], "-") == 0)
        printf("%s\n", after);
    return 0;
}

static int pwd(int argc, char **argv)
{
    char cwd[BS_PATH_MAX];

    (void)argc;
    (void)argv;
    getcwd(cwd, sizeof(cwd));
    printf("%s\n", cwd);
    return 0;
}

static int exit_builtin(int argc, char **argv)
{
    int status = argc > 1 ? atoi(argv[1]) : last_status;

    request_exit(status);
    return status;
}

static int history(int argc, char **argv)
{
    char number[12];
    int count = history_count(), from = 0;

    if (argc > 1 && strcmp(argv[1], "-c") == 0) {
        history_clear();
        return 0;
    }
    if (argc > 1)
        from = count - atoi(argv[1]);
    if (from < 0)
        from = 0;
    for (int i = from; i < count; i++) {
        format_uint((unsigned int)i + 1, number);
        put(C_DIM);
        for (int pad = (int)strlen(number); pad < 4; pad++)
            put(" ");
        printf("%s" C_RESET "  %s\n", number, history_entry(i));
    }
    return 0;
}

static int set(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    var_list();
    return 0;
}

static int unset(int argc, char **argv)
{
    for (int i = 1; i < argc; i++)
        var_unset(argv[i]);
    return 0;
}

static int alias(int argc, char **argv)
{
    int status = 0;

    if (argc == 1)
        alias_list();
    for (int i = 1; i < argc; i++) {
        char *equals = strchr(argv[i], '=');

        if (!equals) {
            const char *value = alias_get(argv[i]);

            if (value)
                printf(C_VAR "%s" C_RESET "='%s'\n", argv[i], value);
            else
                status = bsh_error(argv[i], "no such alias");
            continue;
        }
        *equals = 0;
        if (equals == argv[i] || strchr(argv[i], '/'))
            status = bsh_error("alias", "the name can't be empty or contain /");
        else if (alias_set(argv[i], equals + 1) < 0)
            status = bsh_error(argv[i], "no room (32 aliases, 159 bytes each)");
    }
    return status;
}

static int unalias(int argc, char **argv)
{
    int status = 0;

    for (int i = 1; i < argc; i++)
        if (alias_unset(argv[i]) < 0)
            status = bsh_error(argv[i], "no such alias");
    return status;
}

static int which(int argc, char **argv)
{
    char path[BS_PATH_MAX];
    int status = 0;

    for (int i = 1; i < argc; i++) {
        if (alias_get(argv[i]))
            printf(C_VAR "%s" C_RESET ": alias for '%s'\n", argv[i], alias_get(argv[i]));
        else if (builtin_find(argv[i]))
            printf(C_BUILTIN "%s" C_RESET ": built into bsh\n", argv[i]);
        else if (find_program(argv[i], path, sizeof(path)) == 0)
            printf(C_PROGRAM "%s" C_RESET "\n", path);
        else
            status = bsh_error(argv[i], "not found");
    }
    return status;
}

static int source(int argc, char **argv)
{
    if (argc < 2)
        return bsh_error(argv[0], "which file?");
    return run_script(argv[1], argc - 1, argv + 1);
}

const struct builtin builtins[] = {
    {"cd", "cd [dir | -]", "change directory; - goes back", cd},
    {"pwd", "pwd", "print the current directory", pwd},
    {"alias", "alias [name=words]", "list, show or define aliases", alias},
    {"unalias", "unalias name...", "forget aliases", unalias},
    {"set", "set", "list shell variables", set},
    {"unset", "unset NAME...", "forget variables", unset},
    {"which", "which name...", "what a name runs: alias, builtin or file", which},
    {"history", "history [n | -c]", "the last n lines, or clear them", history},
    {"source", "source file [args]", "run a script in this shell (also: .)", source},
    {".", ". file [args]", "same as source", source},
    {"help", "help", "this screen", help},
    {"exit", "exit [status]", "leave bsh, or end the running script", exit_builtin},
    {0, 0, 0, 0},
};

const struct builtin *builtin_find(const char *name)
{
    for (int i = 0; builtins[i].name; i++)
        if (strcmp(builtins[i].name, name) == 0)
            return &builtins[i];
    return 0;
}
