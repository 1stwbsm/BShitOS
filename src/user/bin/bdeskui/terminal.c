/*
 * Terminal: runs command lines through bsh (so globs, quotes, && and $VARS all
 * work), captures what they print and shows it here. cd, pwd and clear are
 * handled here, as they change this window. Programs that need the screen or
 * the TTY get it (see /conf/bdeskui.conf).
 */
#include "bdeskui.h"

#define LINES_MAX 400
#define LINE_MAX 104
#define INPUT_MAX 120
#define HISTORY_MAX 16
#define ARGS_MAX 16
#define PAD 8

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

#define SCREEN_BG 0x0C171Cu

static unsigned int color_of(int kind)
{
    switch (kind) {
    case COLOR_PROMPT: return 0x8CE87C;
    case COLOR_ERROR: return 0xFF7A6E;
    case COLOR_NOTE: return 0x86A0AC;
    default: return 0xD6E4EA;
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
        int rc = chdir(argc > 1 ? argv[1] : bdeskui_home());

        if (rc < 0)
            add_line(strerror(rc), COLOR_ERROR);
    } else if (strcmp(argv[0], "pwd") == 0) {
        add_line(cwd, COLOR_NORMAL);
    } else if (strcmp(argv[0], "clear") == 0) {
        line_count = 0;
    } else if (strcmp(argv[0], "help") == 0) {
        add_line("bsh command lines run here: ls *.txt, cat x && echo ok, echo $HOME ...", COLOR_NOTE);
        add_line("web, cube and edit take the screen; su, passwd and install use the TTY.", COLOR_NOTE);
        add_line("cd, pwd and clear act on this window. PgUp/PgDn scroll. 'help' in bsh: bsh -c help", COLOR_NOTE);
    } else if (bdeskui_needs_screen(argv[0]) || bdeskui_needs_tty(argv[0])) {
        bdeskui_run_program(argc, argv);
        add_line("(back in BDeskUI)", COLOR_NOTE);
    } else {
        char *shell_argv[3] = {"bsh", "-c", (char *)command};
        int code;

        console_clear();
        code = spawn("/bin/bsh.bs", 3, shell_argv);
        capture_console();
        console_clear();
        if (code < 0)
            add_line(strerror(code), COLOR_ERROR);
        else if (code == BS_EXIT_STOPPED)
            add_line("(stopped for the shutdown)", COLOR_NOTE);
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
        add_line("bsh, inside BDeskUI. 'help' says what works here.", COLOR_NOTE);
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

    gfx_inset(3, 3, w - 6, h - 6, SCREEN_BG);
    if (first < 0)
        first = 0;
    for (int i = 0; i < visible && first + i < line_count; i++)
        gfx_text_n(PAD, PAD + i * LINE_H, lines[first + i], columns, color_of(line_colors[first + i]));

    getcwd(cwd, sizeof(cwd));
    copy_string(prompt, sizeof(prompt), cwd);
    copy_string(prompt + strlen(prompt), sizeof(prompt) - strlen(prompt), "$ ");
    prompt_length = (int)strlen(prompt);
    y = h - PAD - LINE_H + 1;
    gfx_fill(4, y - 4, w - 8, 1, 0x2A3E48);
    if (focused && wm_frame() / 25 % 2 == 0) /* a block caret, under the text */
        gfx_fill(PAD + (prompt_length + cursor) * CHAR_W - 1, y - 1, 7, 9, 0x2F7A3A);
    gfx_text_n(PAD, y, prompt, columns, color_of(COLOR_PROMPT));
    gfx_text_n(PAD + prompt_length * CHAR_W, y, input, columns - prompt_length, color_of(COLOR_NORMAL));
    if (scroll_back)
        gfx_text(w - 15 * CHAR_W, PAD, "[scrolled up]", color_of(COLOR_NOTE));
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
    .name = "BSterm", .blurb = "run programs", .accent = 0x3E9E4E, .icon = ICON_TERMINAL,
    .default_w = 430, .default_h = 250, .min_w = 200, .min_h = 80,
    .open = terminal_open, .draw = terminal_draw, .key = terminal_key,
};
