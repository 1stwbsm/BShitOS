/*
 * The line editor. The line is redrawn from the prompt on every key, coloured
 * as bsh will read it: builtins, aliases and programs in their own colours,
 * unknown commands in red, so typos show before you press Enter.
 */
#include "bsh.h"

#define HISTORY_MAX 100
#define HISTORY_WIDTH 256
#define CTRL(c) ((c) & 0x1F)
#define ESC 0x1B

static char history[HISTORY_MAX][HISTORY_WIDTH];
static int history_used;

static char *buf;
static unsigned int size, len, pos;
static const char *prompt;
static char yank[LINE_MAX];

/* ---- History ------------------------------------------------------------- */

static int history_path(char *path)
{
    const char *home = var_get("HOME");

    path[0] = 0;
    return home && append(path, BS_PATH_MAX, home) == 0 && append(path, BS_PATH_MAX, "/.bsh_history") == 0;
}

static void remember(const char *line)
{
    unsigned int length = strlen(line);

    if (length == 0 || length >= HISTORY_WIDTH)
        return;
    if (history_used && strcmp(history[history_used - 1], line) == 0)
        return;
    if (history_used == HISTORY_MAX) {
        memcpy(history[0], history[1], sizeof(history) - HISTORY_WIDTH);
        history_used--;
    }
    memcpy(history[history_used++], line, length + 1);
}

/* Rewritten whole: BSFS files are written in one go. Fails quietly when live. */
static void history_save(void)
{
    char path[BS_PATH_MAX];
    int fd;

    if (!history_path(path) || (fd = open(path, BS_O_WRITE)) < 0)
        return;
    for (int i = 0; i < history_used; i++) {
        write(fd, history[i], strlen(history[i]));
        write(fd, "\n", 1);
    }
    close(fd);
}

void history_load(void)
{
    static char chunk[1024];
    char line[HISTORY_WIDTH], path[BS_PATH_MAX];
    unsigned int length = 0;
    int fd, got;

    if (!history_path(path) || (fd = open(path, BS_O_READ)) < 0)
        return;
    while ((got = read(fd, chunk, sizeof(chunk))) > 0) {
        for (int i = 0; i < got; i++) {
            if (chunk[i] == '\n') {
                line[length] = 0;
                remember(line);
                length = 0;
            } else if (length < sizeof(line) - 1) {
                line[length++] = chunk[i];
            }
        }
    }
    close(fd);
}

/* A leading space keeps a line out of history. */
void history_add(const char *line)
{
    if (line[0] == ' ')
        return;
    remember(line);
    history_save();
}

int history_count(void)
{
    return history_used;
}

const char *history_entry(int i)
{
    return i >= 0 && i < history_used ? history[i] : 0;
}

void history_clear(void)
{
    history_used = 0;
    history_save();
}

/* ---- Drawing ------------------------------------------------------------ */

static int is_break(char c)
{
    return c == ' ' || c == '\t' || c == ';' || c == '&' || c == '|';
}

static void colour_arguments(const char *s, unsigned int n)
{
    unsigned int i = 0;

    while (i < n) {
        unsigned int j = i + 1;

        if (s[i] == '\'' || s[i] == '"') {
            while (j < n && s[j] != s[i])
                j++;
            if (j < n)
                j++;
            put(C_STRING);
            put_n(s + i, j - i);
            put(C_RESET);
        } else if (s[i] == '$') {
            if (j < n && (s[j] == '?' || s[j] == '#' || (s[j] >= '0' && s[j] <= '9'))) {
                j++;
            } else if (j < n && s[j] == '{') {
                while (j < n && s[j] != '}')
                    j++;
                if (j < n)
                    j++;
            } else {
                while (j < n && is_name_char(s[j], j == i + 1))
                    j++;
            }
            put(C_VAR);
            put_n(s + i, j - i);
            put(C_RESET);
        } else if (s[i] == '\\') {
            j = i + 2 <= n ? i + 2 : n;
            put_n(s + i, j - i);
        } else {
            while (j < n && s[j] != '\'' && s[j] != '"' && s[j] != '$' && s[j] != '\\')
                j++;
            put_n(s + i, j - i);
        }
        i = j;
    }
}

static void colour_line(const char *s, unsigned int n)
{
    unsigned int i = 0;
    int command_position = 1;

    while (i < n) {
        unsigned int start = i;
        char quote = 0;

        if (s[i] == ' ' || s[i] == '\t') {
            put_n(s + i++, 1);
            continue;
        }
        if (s[i] == ';' || s[i] == '&' || s[i] == '|') {
            i += i + 1 < n && s[i + 1] == s[i] && s[i] != ';' ? 2 : 1;
            put(C_DIR);
            put_n(s + start, i - start);
            put(C_RESET);
            command_position = 1;
            continue;
        }
        if (s[i] == '#') {
            put(C_DIM);
            put_n(s + i, n - i);
            put(C_RESET);
            return;
        }

        while (i < n && (quote || !is_break(s[i]))) {
            if (quote && s[i] == quote)
                quote = 0;
            else if (!quote && (s[i] == '\'' || s[i] == '"'))
                quote = s[i];
            else if (!quote && s[i] == '\\')
                i++;
            i++;
        }
        if (i > n)
            i = n;

        if (command_position) {
            const char *equals = 0;
            char word[BS_PATH_MAX];
            unsigned int w = 0;

            for (unsigned int k = start; k < i && !equals; k++)
                if (s[k] == '=')
                    equals = s + k;
            if (equals && valid_name(s + start, (unsigned int)(equals - (s + start)))) {
                colour_arguments(s + start, i - start); /* NAME=value: the command comes next */
                continue;
            }
            for (unsigned int k = start; k < i && w < sizeof(word) - 1; k++)
                if (s[k] != '\'' && s[k] != '"' && s[k] != '\\')
                    word[w++] = s[k];
            word[w] = 0;
            switch (command_kind(word)) {
            case KIND_BUILTIN: put(C_BUILTIN); break;
            case KIND_ALIAS:   put(C_VAR); break;
            case KIND_PROGRAM: put(C_PROGRAM); break;
            default:           put(C_ERROR); break;
            }
            put_n(s + start, i - start);
            put(C_RESET);
            command_position = 0;
        } else {
            colour_arguments(s + start, i - start);
        }
    }
}

/* Back to the saved spot where the prompt starts, then everything again. */
static void render(void)
{
    put("\033[u\033[J");
    put(prompt);
    colour_line(buf, len);
    if (len > pos)
        printf("\033[%uD", len - pos);
}

/* ---- Editing ------------------------------------------------------------- */

static void insert(const char *s, unsigned int n)
{
    if (len + n > size - 1)
        n = size - 1 - len;
    move_bytes(buf + pos + n, buf + pos, len - pos);
    memcpy(buf + pos, s, n);
    len += n;
    pos += n;
}

static void cut(unsigned int from, unsigned int to, int keep)
{
    if (to <= from)
        return;
    if (keep) {
        memcpy(yank, buf + from, to - from);
        yank[to - from] = 0;
    }
    move_bytes(buf + from, buf + to, len - to);
    len -= to - from;
    pos = pos > to ? pos - (to - from) : pos > from ? from : pos;
}

static void set_line(const char *s, unsigned int n)
{
    if (n > size - 1)
        n = size - 1;
    memcpy(buf, s, n);
    len = pos = n;
}

#define KEY_SHUTDOWN (-1)

/* Waits for a key; gives up if a shutdown starts (checked now and then: it costs a syscall). */
static int next_key(void)
{
    int key;

    for (unsigned int idle = 1; !(key = poll_key()); idle++)
        if ((idle & 63) == 0 && shutting_down())
            return KEY_SHUTDOWN;
    return key;
}

/* ---- Tab completion ------------------------------------------------------ */

static char matches[NAMES_MAX][BS_NAME_MAX + 1];
static int match_count;

static void add_match(const char *name, int directory)
{
    unsigned int length = strlen(name);

    if (match_count >= NAMES_MAX || length + 1 >= sizeof(matches[0]))
        return;
    for (int i = 0; i < match_count; i++)
        if (strncmp(matches[i], name, length) == 0 && (matches[i][length] == 0 || matches[i][length] == '/'))
            return;
    memcpy(matches[match_count], name, length);
    matches[match_count][length] = directory ? '/' : 0;
    matches[match_count][length + 1] = 0;
    match_count++;
}

static void insert_escaped(const char *s, unsigned int n)
{
    for (unsigned int i = 0; i < n; i++) {
        if (s[i] == ' ')
            insert("\\", 1);
        insert(s + i, 1);
    }
}

static void list_matches(int commands)
{
    const char *sorted[NAMES_MAX];
    int width = 0, screen_cols, screen_rows, per_row, rows;
    unsigned int at = pos;

    for (int i = 0; i < match_count; i++) {
        sorted[i] = matches[i];
        for (int j = i; j > 0 && strcmp(sorted[j - 1], sorted[j]) > 0; j--) {
            const char *t = sorted[j];

            sorted[j] = sorted[j - 1];
            sorted[j - 1] = t;
        }
        if ((int)strlen(matches[i]) > width)
            width = (int)strlen(matches[i]);
    }
    console_size(&screen_cols, &screen_rows);
    width += 2;
    per_row = screen_cols / width > 0 ? screen_cols / width : 1;
    rows = (match_count + per_row - 1) / per_row;

    pos = len; /* finish the line visibly, list below it, then start a fresh prompt */
    render();
    put("\n");
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < per_row; c++) {
            int i = c * rows + r;
            const char *name = sorted[i < match_count ? i : 0];

            if (i >= match_count)
                continue;
            if (ends_with(name, "/"))
                put(C_DIR);
            else if (commands)
                put(command_kind(name) == KIND_BUILTIN ? C_BUILTIN : command_kind(name) == KIND_ALIAS ? C_VAR : C_PROGRAM);
            put_padded(name, c + 1 < per_row ? (unsigned int)width : 0);
            put(C_RESET);
        }
        put("\n");
    }
    put("\033[s");
    pos = at;
}

static void complete(void)
{
    char word[BS_PATH_MAX], dir[BS_PATH_MAX], prefix[BS_NAME_MAX + 1];
    const char *base;
    unsigned int start = pos, before, base_length, common;
    int commands;

    while (start > 0 && !is_break(buf[start - 1]))
        start--;
    before = start;
    while (before > 0 && (buf[before - 1] == ' ' || buf[before - 1] == '\t'))
        before--;
    commands = before == 0 || buf[before - 1] == ';' || buf[before - 1] == '&' || buf[before - 1] == '|';
    if (pos - start >= sizeof(word))
        return;
    memcpy(word, buf + start, pos - start);
    word[pos - start] = 0;

    match_count = 0;
    if (commands && !strchr(word, '/')) {
        base = word;
        for (int i = 0; i < command_count(); i++)
            if (starts_with(command_name(i), word))
                add_match(command_name(i), 0);
    } else {
        const char *slash = 0;
        struct bs_dirent entry;
        int fd;

        for (const char *p = word; *p; p++)
            if (*p == '/')
                slash = p;
        base = slash ? slash + 1 : word;
        copy_string(prefix, base, sizeof(prefix)); /* word gets cut down to the directory below */
        base = prefix;
        dir[0] = 0;
        if (!slash) {
            append(dir, sizeof(dir), ".");
        } else if (word[0] == '~' && word[1] == '/') {
            append(dir, sizeof(dir), var_get("HOME") ? var_get("HOME") : "");
            word[slash - word + 1] = 0;
            append(dir, sizeof(dir), word + 1);
        } else {
            word[slash - word + 1] = 0;
            append(dir, sizeof(dir), word);
        }
        fd = open(dir, BS_O_READ);
        if (fd < 0)
            return;
        while (readdir(fd, &entry) > 0)
            if ((entry.name[0] != '.' || base[0] == '.') && starts_with(entry.name, base))
                add_match(entry.name, entry.type == BS_TYPE_DIR);
        close(fd);
    }
    if (match_count == 0)
        return;

    base_length = strlen(base);
    common = strlen(matches[0]);
    for (int i = 1; i < match_count; i++) {
        unsigned int k = 0;

        while (k < common && matches[i][k] == matches[0][k])
            k++;
        common = k;
    }
    if (match_count == 1) {
        unsigned int length = strlen(matches[0]);
        int directory = matches[0][length - 1] == '/';

        insert_escaped(matches[0] + base_length, length - base_length - (unsigned int)directory);
        insert(directory ? "/" : " ", 1);
    } else if (common > base_length) {
        insert_escaped(matches[0] + base_length, common - base_length);
    } else {
        list_matches(commands && !strchr(buf + start, '/'));
    }
}

/* ---- Ctrl+R: search history for a substring, newest first ----------------- */

static int find_older(const char *query, int from)
{
    for (int i = from; i >= 0 && query[0]; i--)
        if (contains(history[i], query))
            return i;
    return -1;
}

/* Returns 1 if the found line should run right away. */
static int search(void)
{
    char query[64], saved[LINE_MAX];
    unsigned int query_length = 0, saved_length = len;
    int found = -1;

    query[0] = 0;
    memcpy(saved, buf, len);
    for (;;) {
        int key;

        put("\033[u\033[J" C_DIM "search: " C_RESET C_STRING);
        put(query);
        put(C_RESET C_DIM " -> " C_RESET);
        if (found >= 0)
            colour_line(history[found], strlen(history[found]));
        else if (query_length)
            put(C_ERROR "nothing" C_RESET);

        key = next_key();
        if (key >= 0x20 && key < 0x7F && query_length < sizeof(query) - 1) {
            query[query_length++] = (char)key;
            query[query_length] = 0;
            found = find_older(query, found >= 0 ? found : history_used - 1);
        } else if (key == '\b') {
            if (query_length)
                query[--query_length] = 0;
            found = find_older(query, history_used - 1);
        } else if (key == CTRL('r')) {
            int older = found > 0 ? find_older(query, found - 1) : -1;

            if (older >= 0)
                found = older;
        } else if (key == ESC || key == CTRL('g') || key == CTRL('c')) {
            set_line(saved, saved_length);
            return 0;
        } else {
            if (found >= 0)
                set_line(history[found], strlen(history[found]));
            return key == '\n';
        }
    }
}

/* ---- The editor --------------------------------------------------------- */

static int finish(void)
{
    pos = len;
    render();
    put("\n");
    buf[len] = 0;
    return (int)len;
}

int edit_line(const char *prompt_text, char *out, unsigned int out_size)
{
    char saved[LINE_MAX];
    unsigned int saved_length = 0;
    int browsing = history_used;

    prompt = prompt_text;
    buf = out;
    size = out_size < LINE_MAX ? out_size : LINE_MAX;
    len = pos = 0;
    put("\033[s");
    render();

    for (;;) {
        int key = next_key(), still_browsing = 0, i;

        switch (key) {
        case KEY_SHUTDOWN:
            put("\n");
            buf[0] = 0;
            return -1;
        case CTRL('q'):
            if (!window_mode)
                break;
            pos = len;
            render();
            put("\n");
            buf[0] = 0;
            return EDIT_DETACH;
        case '\n':
            return finish();
        case CTRL('c'):
            pos = len;
            render();
            put(C_DIM "^C" C_RESET "\n");
            buf[0] = 0;
            return -1;
        case '\b':
            if (pos)
                cut(pos - 1, pos, 0);
            break;
        case BS_KEY_DEL:
        case CTRL('d'):
            if (pos < len)
                cut(pos, pos + 1, 0);
            break;
        case BS_KEY_LEFT:
        case CTRL('b'):
            if (pos)
                pos--;
            break;
        case BS_KEY_RIGHT:
        case CTRL('f'):
            if (pos < len)
                pos++;
            break;
        case BS_KEY_HOME:
        case CTRL('a'):
            pos = 0;
            break;
        case BS_KEY_END:
        case CTRL('e'):
            pos = len;
            break;
        case CTRL('u'):
            cut(0, pos, 1);
            break;
        case CTRL('k'):
            cut(pos, len, 1);
            break;
        case CTRL('w'):
            i = (int)pos;
            while (i > 0 && buf[i - 1] == ' ')
                i--;
            while (i > 0 && buf[i - 1] != ' ')
                i--;
            cut((unsigned int)i, pos, 1);
            break;
        case CTRL('y'):
            insert(yank, strlen(yank));
            break;
        case CTRL('l'):
            put("\033[2J\033[s");
            break;
        case '\t':
            complete();
            break;
        case CTRL('r'):
            if (search())
                return finish();
            break;
        case BS_KEY_UP:
            /* What is typed so far filters the history. */
            if (browsing == history_used) {
                memcpy(saved, buf, len);
                saved_length = len;
            }
            for (i = browsing - 1; i >= 0; i--)
                if (strncmp(history[i], saved, saved_length) == 0 &&
                    (strlen(history[i]) != len || strncmp(history[i], buf, len) != 0))
                    break;
            if (i >= 0) {
                browsing = i;
                set_line(history[i], strlen(history[i]));
            }
            still_browsing = 1;
            break;
        case BS_KEY_DOWN:
            for (i = browsing + 1; i < history_used; i++)
                if (strncmp(history[i], saved, saved_length) == 0)
                    break;
            if (browsing < history_used && i < history_used) {
                browsing = i;
                set_line(history[i], strlen(history[i]));
            } else if (browsing < history_used) {
                browsing = history_used;
                set_line(saved, saved_length);
            }
            still_browsing = 1;
            break;
        default:
            if (key >= 0x20 && key < 0x7F) {
                char c = (char)key;

                insert(&c, 1);
            }
            break;
        }
        if (!still_browsing)
            browsing = history_used;
        render();
    }
}
