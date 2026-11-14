/*
 * Terminal: runs BShitOS programs. A program's console output is captured and
 * shown here; programs listed as full-screen get the whole display instead.
 */
#include "bs.h"
#include "desk.h"

#define LINES_MAX 400
#define LINE_MAX 104
#define INPUT_MAX 120
#define HISTORY_MAX 16
#define ARGS_MAX 16
#define PAD 4

static char lines[LINES_MAX][LINE_MAX];
static unsigned char line_colors[LINES_MAX];
static int line_count, scroll_back;
static char input[INPUT_MAX];
static int input_length, cursor;
static char history[HISTORY_MAX][INPUT_MAX];
static int history_count, history_view;
static int welcomed;

enum { COLOR_NORMAL, COLOR_PROMPT, COLOR_ERROR, COLOR_NOTE };

static void add_line(const char *text, int color)
{
    if (line_count == LINES_MAX) {
        for (int i = 1; i < LINES_MAX; i++) {
            memcpy(lines[i - 1], lines[i], LINE_MAX);
            line_colors[i - 1] = line_colors[i];
        }
        line_count--;
    }
    copy_string(lines[line_count], LINE_MAX, text);
    line_colors[line_count] = (unsigned char)color;
    line_count++;
    scroll_back = 0;
}

static unsigned int color_of(int kind)
{
    switch (kind) {
    case COLOR_PROMPT: return app_terminal.accent;
    case COLOR_ERROR: return 0xE0564A;
    case COLOR_NOTE: return theme->dim;
    default: return theme->text;
    }
}

static int split(char *line, char **argv)
{
    int argc = 0;

    while (*line && argc < ARGS_MAX) {
        while (*line == ' ')
            *line++ = 0;
        if (!*line)
            break;
        argv[argc++] = line;
        while (*line && *line != ' ')
            line++;
    }
    return argc;
}

/* Collects what a program printed from the (hidden) text console. */
static void capture_console(void)
{
    char text[LINE_MAX];
    int cols, rows, last = -1;

    console_size(&cols, &rows);
    for (int r = 0; r < rows; r++)
        if (console_line(r, text, sizeof(text)) > 0)
            last = r;
    for (int r = 0; r <= last; r++) {
        console_line(r, text, sizeof(text));
        add_line(text, COLOR_NORMAL);
    }
}

static void run_line(const char *command)
{
    char buffer[INPUT_MAX], cwd[BS_PATH_MAX], echo[LINE_MAX + 8];
    char *argv[ARGS_MAX];
    int argc;

    getcwd(cwd, sizeof(cwd));
    copy_string(echo, sizeof(echo), cwd);
    copy_string(echo + strlen(echo), sizeof(echo) - strlen(echo), "$ ");
    copy_string(echo + strlen(echo), sizeof(echo) - strlen(echo), command);
    add_line(echo, COLOR_PROMPT);

    copy_string(buffer, sizeof(buffer), command);
    argc = split(buffer, argv);
    if (argc == 0)
        return;

    if (strcmp(argv[0], "cd") == 0) {
        int rc = chdir(argc > 1 ? argv[1] : "/userspace/user");

        if (rc < 0)
            add_line(strerror(rc), COLOR_ERROR);
    } else if (strcmp(argv[0], "pwd") == 0) {
        add_line(cwd, COLOR_NORMAL);
    } else if (strcmp(argv[0], "clear") == 0) {
        line_count = 0;
    } else if (strcmp(argv[0], "help") == 0) {
        add_line("Type any BShitOS command: ls, cat, cp, mv, rm, mkdir, write, net, ping, date...", COLOR_NOTE);
        add_line("Full-screen programs (edit, web, cube) take over the display until they exit.", COLOR_NOTE);
        add_line("cd, pwd and clear work here too. PgUp/PgDn scroll.", COLOR_NOTE);
    } else if (desk_is_fullscreen(argv[0])) {
        desk_run_fullscreen(argc, argv);
        add_line("(back from full screen)", COLOR_NOTE);
    } else {
        char path[BS_PATH_MAX];
        int code;

        if (strchr(argv[0], '/')) {
            copy_string(path, sizeof(path), argv[0]);
        } else {
            copy_string(path, sizeof(path), "/bin/");
            copy_string(path + 5, sizeof(path) - 5, argv[0]);
            copy_string(path + strlen(path), sizeof(path) - strlen(path), ".bs");
        }
        console_clear();
        code = spawn(path, argc, argv);
        capture_console();
        console_clear();
        if (code == -BS_ENOENT)
            add_line("command not found (try 'help')", COLOR_ERROR);
        else if (code < 0)
            add_line(strerror(code), COLOR_ERROR);
    }
}

static void remember(const char *command)
{
    if (!command[0])
        return;
    if (history_count == HISTORY_MAX) {
        for (int i = 1; i < HISTORY_MAX; i++)
            memcpy(history[i - 1], history[i], INPUT_MAX);
        history_count--;
    }
    copy_string(history[history_count++], INPUT_MAX, command);
}

static void terminal_open(const char *arg)
{
    if (!welcomed) {
        add_line("BShitOS terminal. Type 'help' for what works here.", COLOR_NOTE);
        welcomed = 1;
    }
    if (arg)
        run_line(arg);
}

void terminal_run(const char *command)
{
    wm_open(&app_terminal, command);
}

static void terminal_draw(int w, int h, int focused)
{
    int visible = (h - PAD * 2) / LINE_H - 1;
    int columns = (w - PAD * 2) / CHAR_W;
    int first = line_count - visible - scroll_back;
    char cwd[BS_PATH_MAX], prompt[BS_PATH_MAX + 4];
    int prompt_length, y;

    gfx_fill(0, 0, w, h, theme->field);
    if (first < 0)
        first = 0;
    for (int i = 0; i < visible && first + i < line_count; i++)
        gfx_text_n(PAD, PAD + i * LINE_H, lines[first + i], columns, color_of(line_colors[first + i]));

    getcwd(cwd, sizeof(cwd));
    copy_string(prompt, sizeof(prompt), cwd);
    copy_string(prompt + strlen(prompt), sizeof(prompt) - strlen(prompt), "$ ");
    prompt_length = (int)strlen(prompt);
    y = h - PAD - LINE_H + 1;
    gfx_fill(0, y - 3, w, 1, theme->line);
    gfx_text_n(PAD, y, prompt, columns, app_terminal.accent);
    gfx_text_n(PAD + prompt_length * CHAR_W, y, input, columns - prompt_length, theme->text);
    if (focused && wm_frame() / 25 % 2 == 0)
        gfx_fill(PAD + (prompt_length + cursor) * CHAR_W, y - 1, 1, 9, theme->text);
    if (scroll_back)
        gfx_text(w - 14 * CHAR_W, PAD, "[scrolled up]", theme->dim);
}

static void terminal_key(int key)
{
    if (key == '\n') {
        char command[INPUT_MAX];

        copy_string(command, sizeof(command), input);
        input_length = cursor = 0;
        input[0] = 0;
        history_view = history_count + 1;
        remember(command);
        history_view = history_count;
        run_line(command);
    } else if (key == '\b') {
        if (cursor > 0) {
            for (int i = cursor - 1; i < input_length; i++)
                input[i] = input[i + 1];
            cursor--;
            input_length--;
        }
    } else if (key == BS_KEY_DEL) {
        if (cursor < input_length) {
            for (int i = cursor; i < input_length; i++)
                input[i] = input[i + 1];
            input_length--;
        }
    } else if (key == BS_KEY_LEFT) {
        if (cursor > 0)
            cursor--;
    } else if (key == BS_KEY_RIGHT) {
        if (cursor < input_length)
            cursor++;
    } else if (key == BS_KEY_HOME) {
        cursor = 0;
    } else if (key == BS_KEY_END) {
        cursor = input_length;
    } else if (key == BS_KEY_UP || key == BS_KEY_DOWN) {
        if (key == BS_KEY_UP && history_view > 0)
            history_view--;
        else if (key == BS_KEY_DOWN && history_view < history_count)
            history_view++;
        copy_string(input, sizeof(input), history_view < history_count ? history[history_view] : "");
        input_length = cursor = (int)strlen(input);
    } else if (key == BS_KEY_PGUP) {
        scroll_back += 10;
        if (scroll_back > line_count)
            scroll_back = line_count;
    } else if (key == BS_KEY_PGDN) {
        scroll_back = scroll_back > 10 ? scroll_back - 10 : 0;
    } else if (key >= 0x20 && key < 0x7F && input_length + 1 < INPUT_MAX) {
        for (int i = input_length; i >= cursor; i--)
            input[i + 1] = input[i];
        input[cursor++] = (char)key;
        input_length++;
    }
}

const struct app app_terminal = {
    .name = "Terminal", .blurb = "run programs", .accent = 0x6CC56C,
    .default_w = 430, .default_h = 250, .min_w = 200, .min_h = 80,
    .open = terminal_open, .draw = terminal_draw, .key = terminal_key,
};
