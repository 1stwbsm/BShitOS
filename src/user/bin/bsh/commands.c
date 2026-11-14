/*
 * Everything that can be typed as a command: builtins, aliases and the programs
 * and scripts in $PATH. Used for live colouring, Tab completion and typo hints.
 */
#include "bsh.h"

static char programs[NAMES_MAX][BS_NAME_MAX];
static int program_count;

static void add_program(const char *file)
{
    unsigned int length = strlen(file);

    if (ends_with(file, ".bsh"))
        length -= 4;
    else if (ends_with(file, ".bs"))
        length -= 3;
    else
        return;
    for (int i = 0; i < program_count; i++)
        if (strncmp(programs[i], file, length) == 0 && programs[i][length] == 0)
            return; /* an earlier $PATH entry wins */
    if (program_count < NAMES_MAX && length > 0) {
        memcpy(programs[program_count], file, length);
        programs[program_count++][length] = 0;
    }
}

void commands_refresh(void)
{
    const char *path = var_get("PATH");
    char dir[BS_PATH_MAX];

    program_count = 0;
    while (path && *path) {
        const char *end = strchr(path, ':');
        unsigned int length = end ? (unsigned int)(end - path) : strlen(path);
        struct bs_dirent entry;
        int fd;

        if (length && length < sizeof(dir)) {
            memcpy(dir, path, length);
            dir[length] = 0;
            fd = open(dir, BS_O_READ);
            if (fd >= 0) {
                while (readdir(fd, &entry) > 0)
                    if (entry.type != BS_TYPE_DIR)
                        add_program(entry.name);
                close(fd);
            }
        }
        path = end ? end + 1 : 0;
    }
}

int command_count(void)
{
    int n = program_count;

    while (builtins[n - program_count].name)
        n++;
    for (int i = 0; alias_name(i); i++)
        n++;
    return n;
}

const char *command_name(int i)
{
    if (i < program_count)
        return programs[i];
    i -= program_count;
    for (int b = 0; builtins[b].name; b++)
        if (i-- == 0)
            return builtins[b].name;
    return alias_name(i);
}

enum command_kind command_kind(const char *word)
{
    char path[BS_PATH_MAX];

    if (alias_get(word))
        return KIND_ALIAS;
    if (builtin_find(word))
        return KIND_BUILTIN;
    if (strchr(word, '/'))
        return find_program(word, path, sizeof(path)) == 0 ? KIND_PROGRAM : KIND_NONE;
    for (int i = 0; i < program_count; i++)
        if (strcmp(programs[i], word) == 0)
            return KIND_PROGRAM;
    return KIND_NONE;
}

/* Optimal string alignment distance, capped: swaps count as one edit. */
static int distance(const char *a, const char *b)
{
    int la = (int)strlen(a), lb = (int)strlen(b);
    int prev2[BS_NAME_MAX + 1], prev[BS_NAME_MAX + 1], cur[BS_NAME_MAX + 1];

    if (la > BS_NAME_MAX || lb > BS_NAME_MAX)
        return 99;
    for (int j = 0; j <= lb; j++)
        prev[j] = j;
    for (int i = 1; i <= la; i++) {
        cur[0] = i;
        for (int j = 1; j <= lb; j++) {
            int cost = a[i - 1] != b[j - 1];
            int best = prev[j - 1] + cost;

            if (prev[j] + 1 < best)
                best = prev[j] + 1;
            if (cur[j - 1] + 1 < best)
                best = cur[j - 1] + 1;
            if (i > 1 && j > 1 && a[i - 1] == b[j - 2] && a[i - 2] == b[j - 1] && prev2[j - 2] + 1 < best)
                best = prev2[j - 2] + 1;
            cur[j] = best;
        }
        memcpy(prev2, prev, sizeof(prev));
        memcpy(prev, cur, sizeof(cur));
    }
    return prev[lb];
}

const char *closest_command(const char *word)
{
    const char *best = 0;
    int best_distance = strlen(word) <= 3 ? 1 : 2;

    if (strlen(word) < 2)
        return 0; /* one character is one edit from everything short */
    for (int i = 0; i < command_count(); i++) {
        const char *name = command_name(i);
        int d;

        if (strlen(name) < 2)
            continue;
        d = distance(word, name);

        if (d <= best_distance && (!best || d < best_distance)) {
            best = name;
            best_distance = d;
        }
    }
    return best;
}
