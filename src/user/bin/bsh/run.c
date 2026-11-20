/*
 * Parsing and running command lines.
 *
 *   a ; b    a && b    a || b    # comment
 *   'raw'    "with $vars"    \x    ~    *.txt    file?.c
 *   NAME=value    $NAME ${NAME} $? $# $0-$9
 *
 * No pipes, redirection or background jobs: BShitOS runs one program at a time
 * and programs only have the console.
 */
#include "bsh.h"

#define TEXT_MAX 2048
#define DEPTH_MAX 5 /* nested scripts, counting the top level */
#define ALIAS_DEPTH 4
#define SCRIPT_MAX 8192

enum { OP_END, OP_SEQ, OP_AND, OP_OR, OP_ERROR = -1 };

struct command {
    int argc;
    char *argv[WORDS_MAX];
    char text[TEXT_MAX];
    unsigned int used;
    int overflow;
};

int last_status;
static int exit_flag, exit_status;
static int positional_count;
static char **positional;
static int depth; /* run_line nesting: the interactive line, then scripts */

/* Indexed by depth, so a script's arguments survive while it runs other lines. */
static struct command line_commands[DEPTH_MAX];
static struct command alias_commands[DEPTH_MAX][ALIAS_DEPTH];
static char script_text[DEPTH_MAX][SCRIPT_MAX];

static int execute(struct command *c, int alias_level);

int exit_requested(void)
{
    return exit_flag;
}

void request_exit(int status)
{
    exit_flag = 1;
    exit_status = status;
}

void set_positional(int argc, char **argv)
{
    positional_count = argc;
    positional = argv;
}

/* ---- Parsing ------------------------------------------------------------- */

static void add_char(struct command *c, char ch)
{
    if (c->used + 1 >= TEXT_MAX) {
        c->overflow = 1;
        return;
    }
    c->text[c->used++] = ch;
}

static void add_text(struct command *c, const char *s)
{
    while (*s)
        add_char(c, *s++);
}

static void add_word(struct command *c, unsigned int start)
{
    if (c->argc >= WORDS_MAX - 1) {
        c->overflow = 1;
        return;
    }
    c->argv[c->argc++] = c->text + start;
}

static int is_delimiter(char ch)
{
    return ch == 0 || ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' || ch == ';' || ch == '&' || ch == '|';
}

/* p is at '$'. Returns where the variable reference ends. */
static const char *expand_variable(const char *p, struct command *c)
{
    char name[32], number[12];
    unsigned int n = 0;
    const char *value;

    p++;
    if (*p == '?' || *p == '#') {
        format_uint(*p == '?' ? (unsigned int)last_status : (unsigned int)(positional_count ? positional_count - 1 : 0), number);
        add_text(c, number);
        return p + 1;
    }
    if (*p >= '0' && *p <= '9') {
        if (*p - '0' < positional_count)
            add_text(c, positional[*p - '0']);
        return p + 1;
    }
    if (*p == '{') {
        const char *end = strchr(p, '}');

        if (!end || end - p - 1 >= (int)sizeof(name) || !valid_name(p + 1, (unsigned int)(end - p - 1))) {
            add_char(c, '$');
            return p;
        }
        n = (unsigned int)(end - p - 1);
        memcpy(name, p + 1, n);
        name[n] = 0;
        p = end + 1;
    } else {
        while (n < sizeof(name) - 1 && is_name_char(p[n], n == 0))
            n++;
        if (n == 0) {
            add_char(c, '$');
            return p;
        }
        memcpy(name, p, n);
        name[n] = 0;
        p += n;
    }
    value = var_get(name);
    if (value)
        add_text(c, value);
    return p;
}

static int glob_match(const char *pattern, const char *s)
{
    if (*pattern == 0)
        return *s == 0;
    if (*pattern == '*') {
        for (;;) {
            if (glob_match(pattern + 1, s))
                return 1;
            if (!*s++)
                return 0;
        }
    }
    if (*s && (*pattern == '?' || *pattern == *s))
        return glob_match(pattern + 1, s + 1);
    return 0;
}

/* Replaces the word at text+start with the files it matches (last path part only). */
static void expand_glob(struct command *c, unsigned int start)
{
    char pattern[BS_PATH_MAX], dir[BS_PATH_MAX];
    char *slash, *base;
    struct bs_dirent entry;
    int first = c->argc, fd;

    copy_string(pattern, c->text + start, sizeof(pattern));
    slash = 0;
    for (char *p = pattern; *p; p++)
        if (*p == '/')
            slash = p;
    base = slash ? slash + 1 : pattern;
    if (slash) {
        unsigned int length = slash == pattern ? 1u : (unsigned int)(slash - pattern);

        memcpy(dir, pattern, length);
        dir[length] = 0;
    } else {
        copy_string(dir, ".", sizeof(dir));
    }
    if (strchr(dir, '*') || strchr(dir, '?') || (fd = open(dir, BS_O_READ)) < 0) {
        add_word(c, start);
        return;
    }

    c->used = start;
    while (readdir(fd, &entry) > 0) {
        unsigned int word = c->used;

        if (entry.name[0] == '.' && base[0] != '.')
            continue;
        if (!glob_match(base, entry.name))
            continue;
        if (slash)
            for (char *p = pattern; p <= slash; p++)
                add_char(c, *p);
        add_text(c, entry.name);
        add_char(c, 0);
        add_word(c, word);
    }
    close(fd);

    if (c->argc == first) { /* nothing matched: keep the word as typed */
        add_text(c, pattern);
        add_char(c, 0);
        add_word(c, start);
        return;
    }
    for (int i = first + 1; i < c->argc; i++) /* sorted, like ls */
        for (int j = i; j > first && strcmp(c->argv[j - 1], c->argv[j]) > 0; j--) {
            char *t = c->argv[j];

            c->argv[j] = c->argv[j - 1];
            c->argv[j - 1] = t;
        }
}

/* Reads one command, up to the operator that ends it. */
static int parse(const char **cursor, struct command *c)
{
    const char *p = *cursor;

    c->argc = 0;
    c->used = 0;
    c->overflow = 0;
    for (;;) {
        unsigned int start;
        int glob = 0, quoted = 0;

        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
            p++;
        if (*p == 0 || *p == '#') {
            *cursor = p + strlen(p);
            return OP_END;
        }
        if (*p == ';') {
            *cursor = p + 1;
            return OP_SEQ;
        }
        if ((p[0] == '&' && p[1] == '&') || (p[0] == '|' && p[1] == '|')) {
            *cursor = p + 2;
            return p[0] == '&' ? OP_AND : OP_OR;
        }
        if (*p == '|')
            return bsh_error("|", "no pipes: BShitOS runs one program at a time"), OP_ERROR;
        if (*p == '&')
            return bsh_error("&", "no background jobs: BShitOS runs one program at a time"), OP_ERROR;

        start = c->used;
        if (*p == '~' && (is_delimiter(p[1]) || p[1] == '/')) {
            const char *home = var_get("HOME");

            add_text(c, home ? home : "/");
            p++;
        }
        while (!is_delimiter(*p)) {
            if (*p == '\\') {
                p++;
                if (*p)
                    add_char(c, *p++);
                quoted = 1;
            } else if (*p == '\'') {
                const char *end = strchr(p + 1, '\'');

                if (!end)
                    return bsh_error("'", "quote never closed"), OP_ERROR;
                while (++p < end)
                    add_char(c, *p);
                p++;
                quoted = 1;
            } else if (*p == '"') {
                p++;
                while (*p && *p != '"') {
                    if (*p == '\\' && (p[1] == '"' || p[1] == '\\' || p[1] == '$')) {
                        add_char(c, p[1]);
                        p += 2;
                    } else if (*p == '$') {
                        p = expand_variable(p, c);
                    } else {
                        add_char(c, *p++);
                    }
                }
                if (!*p)
                    return bsh_error("\"", "quote never closed"), OP_ERROR;
                p++;
                quoted = 1;
            } else if (*p == '$') {
                p = expand_variable(p, c);
            } else {
                if (*p == '*' || *p == '?')
                    glob = 1;
                add_char(c, *p++);
            }
        }
        if (c->used == start && !quoted)
            continue; /* an empty, unquoted $VAR vanishes */
        add_char(c, 0);
        if (glob)
            expand_glob(c, start);
        else
            add_word(c, start);
        if (c->overflow)
            return bsh_error("line", "too long (31 words, 2 KiB once expanded)"), OP_ERROR;
    }
}

/* ---- Running --------------------------------------------------------------- */

static int is_file(const char *path)
{
    struct bs_dirent entry;
    int fd = open(path, BS_O_READ), rc;

    if (fd < 0)
        return 0;
    rc = readdir(fd, &entry); /* fails on anything that is not a directory */
    close(fd);
    return rc < 0;
}

static int try_path(char *path, unsigned int size, const char *dir, const char *name, const char *extension)
{
    path[0] = 0;
    if ((dir && (append(path, size, dir) < 0 || append(path, size, "/") < 0)) ||
        append(path, size, name) < 0 || append(path, size, extension) < 0)
        return 0;
    return is_file(path);
}

int find_program(const char *name, char *path, unsigned int size)
{
    const char *search = var_get("PATH");
    char dir[BS_PATH_MAX];

    if (strchr(name, '/'))
        return try_path(path, size, 0, name, "") || try_path(path, size, 0, name, ".bs") ||
               try_path(path, size, 0, name, ".bsh") ? 0 : -1;
    while (search && *search) {
        const char *end = strchr(search, ':');
        unsigned int length = end ? (unsigned int)(end - search) : strlen(search);

        if (length && length < sizeof(dir)) {
            memcpy(dir, search, length);
            dir[length] = 0;
            if (try_path(path, size, dir, name, ".bs") || try_path(path, size, dir, name, ".bsh"))
                return 0;
        }
        search = end ? end + 1 : 0;
    }
    return -1;
}

static int not_found(const char *name)
{
    const char *guess = strchr(name, '/') ? 0 : closest_command(name);

    printf(C_ERROR "bsh: %s: no such command" C_RESET, name);
    if (!strchr(name, '/') && is_file(name)) /* only $PATH is searched, not here */
        printf(C_DIM " (it's right here: " C_RESET C_BOLD "./%s" C_RESET C_DIM ")" C_RESET, name);
    else if (guess)
        printf(C_DIM " (did you mean " C_RESET C_BOLD "%s" C_RESET C_DIM "?)" C_RESET, guess);
    printf("\n");
    return 127;
}

static int run_command(int argc, char **argv)
{
    const struct builtin *builtin = builtin_find(argv[0]);
    char path[BS_PATH_MAX];
    int code;

    if (builtin)
        return builtin->run(argc, argv);
    if (find_program(argv[0], path, sizeof(path)) < 0)
        return not_found(argv[0]);
    if (ends_with(path, ".bsh"))
        return run_script(path, argc, argv);

    code = spawn(path, argc, argv);
    if (code == -BS_ENOEXEC)
        return bsh_error(argv[0], "not a BShitOS program"), 126;
    if (code < 0)
        return bsh_error(argv[0], strerror(code)), code == -BS_ENOENT ? 127 : 126;
    return code;
}

static int execute(struct command *c, int alias_level)
{
    char **argv = c->argv;
    int argc = c->argc;
    const char *alias;

    /* Leading NAME=value words set shell variables. */
    while (argc > 0) {
        char *equals = strchr(argv[0], '=');

        if (!equals || !valid_name(argv[0], (unsigned int)(equals - argv[0])))
            break;
        *equals = 0;
        if (var_set(argv[0], equals + 1) < 0)
            bsh_error(argv[0], "no room (48 variables, 255 bytes each)");
        argv++;
        argc--;
    }
    if (argc == 0)
        return 0;

    alias = alias_level < ALIAS_DEPTH ? alias_get(argv[0]) : 0;
    if (alias) {
        struct command *a = &alias_commands[depth - 1][alias_level];
        const char *p = alias;
        int op = parse(&p, a);

        if (op == OP_ERROR)
            return 2;
        if (op != OP_END)
            return bsh_error(argv[0], "aliases are plain words; put ; && || in a script"), 2;
        for (int i = 1; i < argc && a->argc < WORDS_MAX - 1; i++)
            a->argv[a->argc++] = argv[i];
        if (a->argc == 0)
            return 0;
        /* alias ls='ls -a' must not expand itself forever */
        return execute(a, strcmp(a->argv[0], argv[0]) == 0 ? ALIAS_DEPTH : alias_level + 1);
    }
    return run_command(argc, argv);
}

int run_line(const char *line)
{
    struct command *c;
    const char *p = line;
    int op = OP_SEQ;

    if (depth >= DEPTH_MAX)
        return bsh_error("script", "nested too deep"), 2;
    c = &line_commands[depth++];
    while (op != OP_END && !exit_flag && !shutting_down()) {
        int run = op == OP_SEQ || (op == OP_AND && last_status == 0) || (op == OP_OR && last_status != 0);
        int next = parse(&p, c);

        if (next == OP_ERROR) {
            last_status = 2;
            break;
        }
        if (run && c->argc)
            last_status = execute(c, 0);
        op = next;
    }
    depth--;
    return last_status;
}

/* Runs a file of command lines. `exit` ends the script, not bsh. */
int run_script(const char *path, int argc, char **argv)
{
    char **saved = positional, *text, *line;
    int saved_count = positional_count, fd, got, total = 0;

    if (depth >= DEPTH_MAX)
        return bsh_error(path, "scripts nested too deep"), 2;
    fd = open(path, BS_O_READ);
    if (fd < 0)
        return bsh_error(path, strerror(fd)), 127;
    text = script_text[depth];
    while (total < SCRIPT_MAX - 1 && (got = read(fd, text + total, (unsigned int)(SCRIPT_MAX - 1 - total))) > 0)
        total += got;
    if (total == SCRIPT_MAX - 1 && read(fd, &got, 1) > 0) {
        close(fd);
        return bsh_error(path, "script too big (8 KiB max)"), 2;
    }
    close(fd);
    text[total] = 0;

    set_positional(argc, argv);
    last_status = 0;
    for (line = text; line && *line && !exit_flag && !shutting_down();) {
        char *end = strchr(line, '\n');

        if (end)
            *end = 0;
        run_line(line);
        line = end ? end + 1 : 0;
    }
    set_positional(saved_count, saved);
    if (exit_flag) {
        exit_flag = 0;
        last_status = exit_status;
    }
    return last_status;
}
