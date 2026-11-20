/*
 * Programs in windows. BShitOS runs one program at a time, so a program started
 * here gets the inside of its window (the kernel's viewport) and BDeskUI waits
 * until it exits, like a modern DOS.
 *
 * A terminal window is the real thing: bsh in the viewport, its output flowing
 * as on any terminal. Ctrl+Q in bsh comes back to the desktop; the window keeps
 * what was on screen and puts it back when you go in again. Programs with a
 * graphical mode (bdeskui.conf windowed=, e.g. web) get a plain window instead.
 */
#include "bdeskui.h"

#define TERMS 6
#define ROWS_MAX 100
#define COLS_MAX 200
#define DETACHED_CODE 200 /* bsh --window's exit code for Ctrl+Q */
#define LIST_MAX 160

enum term_state { TERM_SHELL, TERM_DETACHED, TERM_EXITED };

struct term {
    int used, program; /* program: a graphical program window */
    char title[32];
    char command[160]; /* the line it runs; empty for an interactive shell */
    enum term_state state;
    int status, rows, cols;
    char text[ROWS_MAX][COLS_MAX + 1];
    unsigned char attr[ROWS_MAX][COLS_MAX];
};

static struct term terms[TERMS];
static char windowed_list[LIST_MAX] = "web";

/* The kernel console's colours (drivers/console.c), for drawing captured text. */
static const unsigned int palette[16] = {
    0x4A4A4A, 0xE0564A, 0x7BC86C, 0xE8C547, 0x5C8DEB, 0xC678DD, 0x56C2D6, 0xD8D8D8,
    0x8A8A8A, 0xFF7A6E, 0x9EE891, 0xFFE070, 0x82AEFF, 0xE3A0F5, 0x84E3F0, 0xFFFFFF,
};
#define TERM_FG 0xD8D8D8u
#define TERM_BG 0x000000u

void terminal_load_conf(void)
{
    bdeskui_conf_get("windowed", windowed_list, sizeof(windowed_list));
}

int is_windowed_program(const char *program)
{
    unsigned int length = strlen(program);
    const char *p = windowed_list;

    while (*p) {
        unsigned int n = 0;

        while (*p == ' ')
            p++;
        while (p[n] && p[n] != ' ')
            n++;
        if (n == length && n && strncmp(p, program, n) == 0)
            return 1;
        p += n;
    }
    return 0;
}

static int free_slot(void)
{
    for (int i = 0; i < TERMS; i++)
        if (!terms[i].used)
            return i;
    return -1;
}

/* ---- Keeping and restoring the screen ------------------------------------------ */

static void capture(struct term *t)
{
    int cols, rows;

    console_size(&cols, &rows);
    t->rows = rows < ROWS_MAX ? rows : ROWS_MAX;
    t->cols = cols < COLS_MAX ? cols : COLS_MAX;
    for (int r = 0; r < t->rows; r++) {
        int got = console_line(r, t->text[r], COLS_MAX + 1);

        if (got < 0)
            t->text[r][0] = 0;
        memset(t->attr[r], 0, COLS_MAX);
        console_attrs(r, t->attr[r], (unsigned int)t->cols);
    }
}

static int last_used_row(const struct term *t)
{
    int last = -1;

    for (int r = 0; r < t->rows; r++)
        if (t->text[r][0])
            last = r;
    return last;
}

/* Writes the kept screen back to the console, colours and all. */
static void restore(const struct term *t)
{
    int last = last_used_row(t);

    if (t->state == TERM_DETACHED && last >= 0)
        last--; /* the prompt Ctrl+Q left: bsh puts a fresh one in its place */

    for (int r = 0; r <= last; r++) {
        unsigned char current = 0;

        for (int c = 0; t->text[r][c]; c++) {
            unsigned char a = t->attr[r][c];

            if (a != current) {
                char sgr[24] = "\033[0";
                unsigned int n = 3, color = a & 0x1F;

                if (a & 0x40) {
                    sgr[n++] = ';';
                    sgr[n++] = '1';
                }
                if (a & 0x20) {
                    sgr[n++] = ';';
                    sgr[n++] = '7';
                }
                if (color) {
                    unsigned int code = color <= 8 ? 30 + color - 1 : 90 + color - 9;

                    sgr[n++] = ';';
                    sgr[n++] = (char)('0' + code / 10);
                    sgr[n++] = (char)('0' + code % 10);
                }
                sgr[n++] = 'm';
                sgr[n] = 0;
                printf("%s", sgr);
                current = a;
            }
            if ((unsigned char)t->text[r][c] >= 0x80 && (unsigned char)t->text[r][c] <= 0x86) {
                /* box shapes go back as the UTF-8 the console turned into them */
                static const char *const utf8[7] = {"\xE2\x96\x88", "\xE2\x95\x90", "\xE2\x95\x91",
                                                    "\xE2\x95\x94", "\xE2\x95\x97", "\xE2\x95\x9A",
                                                    "\xE2\x95\x9D"};

                printf("%s", utf8[(unsigned char)t->text[r][c] - 0x80]);
            } else {
                printf("%c", t->text[r][c]);
            }
        }
        printf("\033[0m\n");
    }
}

/* ---- Running ------------------------------------------------------------------ */

/* The directory a detached bsh left in, so going back in lands there. */
static void follow_shell_cwd(void)
{
    char path[BS_PATH_MAX], cwd[BS_PATH_MAX];
    int fd, got;

    copy_string(path, sizeof(path), bdeskui_home());
    copy_string(path + strlen(path), sizeof(path) - strlen(path), "/.bsh_cwd");
    fd = open(path, BS_O_READ);
    if (fd < 0)
        return;
    got = read(fd, cwd, sizeof(cwd) - 1);
    close(fd);
    if (got > 0) {
        cwd[got] = 0;
        chdir(cwd);
    }
}

static void enter(int i)
{
    struct term *t = &terms[i];
    const struct app *app = t->program ? &app_program : &app_terminal;
    char path[BS_PATH_MAX], program[32], *argv[5];
    int sx, sy, sw, sh, argc = 0, code;

    if (wm_open(app, i, 0) < 0)
        return;
    if (wm_content_rect(app, i, &sx, &sy, &sw, &sh) < 0) /* moves it fully on screen first */
        return;
    wm_render(0, 0, 0); /* the desktop as it waits, this window in front */
    viewport(sx, sy, sw, sh);

    if (t->program) {
        unsigned int n = 0;

        while (t->command[n] && t->command[n] != ' ' && n + 1 < sizeof(program)) {
            program[n] = t->command[n];
            n++;
        }
        program[n] = 0;
        copy_string(path, sizeof(path), "/bin/");
        copy_string(path + 5, sizeof(path) - 5, program);
        copy_string(path + strlen(path), sizeof(path) - strlen(path), ".bs");
        argv[argc++] = program;
        argv[argc++] = "--graphical";
        if (t->command[n] == ' ')
            argv[argc++] = t->command + n + 1;
        code = spawn(path, argc, argv);
        viewport(0, 0, 0, 0);
        if (code < 0) { /* say why, in a terminal */
            char line[96] = "echo could not start ";

            copy_string(line + strlen(line), sizeof(line) - strlen(line), program);
            t->used = 0;
            wm_close_instance(app, i);
            terminal_new(line);
            return;
        }
        t->used = 0;
        wm_close_instance(app, i);
        return;
    }

    if (t->state == TERM_SHELL && !t->command[0] && t->rows == 0)
        printf("\033[90mBSterm: bsh in a window. Ctrl+Q: back to the desktop.\033[0m\n");
    else
        restore(t);
    if (t->state == TERM_DETACHED)
        follow_shell_cwd();
    argv[argc++] = "bsh";
    argv[argc++] = "--window";
    if (t->command[0]) {
        argv[argc++] = "-c";
        argv[argc++] = t->command;
    }
    code = spawn("/bin/bsh.bs", argc, argv);
    capture(t);
    viewport(0, 0, 0, 0);

    if (code == DETACHED_CODE) {
        t->state = TERM_DETACHED;
    } else if (t->command[0]) {
        t->state = TERM_EXITED;
        t->status = code;
    } else { /* exit in the shell closes its window */
        t->used = 0;
        wm_close_instance(app, i);
    }
}

void terminal_new(const char *command)
{
    int i = free_slot();
    struct term *t;

    if (i < 0)
        return;
    t = &terms[i];
    memset(t, 0, sizeof(*t));
    t->used = 1;
    copy_string(t->command, sizeof(t->command), command ? command : "");
    copy_string(t->title, sizeof(t->title), "BSterm");
    if (command) {
        unsigned int n = strlen(t->title);

        copy_string(t->title + n, sizeof(t->title) - n, ": ");
        copy_string(t->title + strlen(t->title), sizeof(t->title) - strlen(t->title), command);
    }
    enter(i);
}

void program_window(const char *program, const char *arg)
{
    int i = free_slot();
    struct term *t;

    if (i < 0)
        return;
    t = &terms[i];
    memset(t, 0, sizeof(*t));
    t->used = 1;
    t->program = 1;
    copy_string(t->command, sizeof(t->command), program);
    if (arg) {
        copy_string(t->command + strlen(t->command), sizeof(t->command) - strlen(t->command), " ");
        copy_string(t->command + strlen(t->command), sizeof(t->command) - strlen(t->command), arg);
    }
    copy_string(t->title, sizeof(t->title), "BS");
    copy_string(t->title + 2, sizeof(t->title) - 2, program);
    enter(i);
}

void launch_command(const char *line)
{
    char program[32];
    unsigned int n = 0;

    while (line[n] == ' ')
        line++;
    while (line[n] && line[n] != ' ' && n + 1 < sizeof(program)) {
        program[n] = line[n];
        n++;
    }
    program[n] = 0;
    if (!n)
        return;
    if (is_windowed_program(program))
        program_window(program, line[n] == ' ' ? line + n + 1 : 0);
    else
        terminal_new(line);
}

void run_full_screen_shell(void)
{
    char *argv[2] = {"bsh", "--window"};

    viewport(0, 0, 0, 0);
    graphics_end();
    console_clear();
    printf("\033[90mbsh on the whole screen. exit or Ctrl+Q: back to BDeskUI.\033[0m\n");
    spawn("/bin/bsh.bs", 2, argv);
    console_clear();
}

/* ---- The windows ---------------------------------------------------------------- */

/* The console's box shapes (drivers/fb.c), at layout scale: strokes in a 6x8 cell. */
static void box_shape(unsigned char shape, int x, int y, int s, unsigned int color)
{
    int v1 = x + s, v2 = x + 3 * s, h1 = y + 2 * s, h2 = y + 4 * s, w = CHAR_W * s, h = CHAR_H * s;

    switch (shape) {
    case 0x80: gfx_fill(x, y, w, h, color); break;
    case 0x81: gfx_fill(x, h1, w, s, color); gfx_fill(x, h2, w, s, color); break;
    case 0x82: gfx_fill(v1, y, s, h, color); gfx_fill(v2, y, s, h, color); break;
    case 0x83: gfx_fill(v1, h1, w - s, s, color); gfx_fill(v1, h1, s, h - 2 * s, color);
               gfx_fill(v2, h2, w - 3 * s, s, color); gfx_fill(v2, h2, s, h - 4 * s, color); break;
    case 0x84: gfx_fill(x, h1, 4 * s, s, color); gfx_fill(v2, h1, s, h - 2 * s, color);
               gfx_fill(x, h2, 2 * s, s, color); gfx_fill(v1, h2, s, h - 4 * s, color); break;
    case 0x85: gfx_fill(v1, y, s, 5 * s, color); gfx_fill(v1, h2, w - s, s, color);
               gfx_fill(v2, y, s, 3 * s, color); gfx_fill(v2, h1, w - 3 * s, s, color); break;
    case 0x86: gfx_fill(v2, y, s, 5 * s, color); gfx_fill(x, h2, 4 * s, s, color);
               gfx_fill(v1, y, s, 3 * s, color); gfx_fill(x, h1, 2 * s, s, color); break;
    default: break;
    }
}

static void terminal_draw(int i, int w, int h, int focused)
{
    const struct term *t = &terms[i];
    int scale = gfx_text_scale(), cell_w = CHAR_W * scale, cell_h = CHAR_H * scale;
    const char *note = 0;

    gfx_fill(0, 0, w, h, TERM_BG);
    for (int r = 0; r < t->rows && (r + 1) * cell_h <= h; r++) {
        for (int c = 0; t->text[r][c] && (c + 1) * cell_w <= w; c++) {
            unsigned char a = t->attr[r][c];
            unsigned int index = a & 0x1F, fg = index ? palette[(a & 0x40) && index <= 8 ? index + 7 : index - 1]
                                                      : TERM_FG;
            char s[2] = {t->text[r][c], 0};

            if (a & 0x20) {
                gfx_fill(c * cell_w, r * cell_h, cell_w, cell_h, fg);
                fg = TERM_BG;
            }
            if ((unsigned char)s[0] >= 0x80)
                box_shape((unsigned char)s[0], c * cell_w, r * cell_h, scale, fg);
            else
                gfx_text_scaled(c * cell_w, r * cell_h, s, scale, fg);
        }
    }
    if (t->state == TERM_DETACHED)
        note = "detached: click here or press Enter to go back in";
    else if (t->state == TERM_EXITED)
        note = "finished. Enter: a shell in this window";
    if (note) {
        gfx_fill(0, h - 11, w, 11, focused ? skin->chrome : DARKER(skin->chrome, 90));
        gfx_text_n(4, h - 9, note, (w - 8) / CHAR_W, skin->chrome_text);
    }
}

static void terminal_reenter(int i)
{
    struct term *t = &terms[i];

    if (!t->used)
        return;
    if (t->state == TERM_EXITED) { /* the command is done: a shell takes over the window */
        t->command[0] = 0;
        copy_string(t->title, sizeof(t->title), "BSterm");
        t->state = TERM_SHELL;
    }
    enter(i);
}

static void terminal_key(int i, int key)
{
    if (key == '\n' || key == ' ')
        terminal_reenter(i);
}

static void terminal_press(int i, int x, int y, int w, int h)
{
    (void)x, (void)y, (void)w, (void)h;
    terminal_reenter(i);
}

static const char *terminal_title(int i)
{
    return terms[i].title;
}

static void terminal_closed(int i)
{
    terms[i].used = 0;
}

static void program_draw(int i, int w, int h, int focused)
{
    (void)i, (void)focused;
    gfx_fill(0, 0, w, h, TERM_BG);
}

const struct app app_terminal = {
    .name = "BSterm", .blurb = "bsh in a window", .icon = ICON_TERMINAL,
    .default_w = 440, .default_h = 260, .min_w = 200, .min_h = 100, .instances = TERMS,
    .draw = terminal_draw, .key = terminal_key, .press = terminal_press,
    .title = terminal_title, .closed = terminal_closed,
};

const struct app app_program = {
    .name = "program", .blurb = "a program's graphical mode", .icon = ICON_PROGRAM,
    .default_w = 560, .default_h = 320, .min_w = 320, .min_h = 200, .instances = TERMS,
    .draw = program_draw, .title = terminal_title, .closed = terminal_closed,
};
