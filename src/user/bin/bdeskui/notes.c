/* Notes: a text editor in a window. Ctrl+S saves. */
#include "bs.h"
#include "bdeskui.h"

#define TEXT_MAX 32768
#define BAR_H 20
#define PAD 8
#define DEFAULT_NAME "/untitled.txt"

static char text[TEXT_MAX];
static int length, caret, top_line, modified;
static char path[BS_PATH_MAX];

/* ~/untitled.txt */
static void default_path(void)
{
    copy_string(path, sizeof(path), bdeskui_home());
    if (strlen(path) + sizeof(DEFAULT_NAME) < sizeof(path))
        memcpy(path + strlen(path), DEFAULT_NAME, sizeof(DEFAULT_NAME));
}
static char status[48];
static int visible_lines = 10, visible_columns = 40;

static int line_start(int position)
{
    while (position > 0 && text[position - 1] != '\n')
        position--;
    return position;
}

static int line_end(int position)
{
    while (position < length && text[position] != '\n')
        position++;
    return position;
}

static int caret_line(void)
{
    int line = 0;

    for (int i = 0; i < caret; i++)
        if (text[i] == '\n')
            line++;
    return line;
}

void notes_open(const char *file)
{
    wm_open(&app_notes, file);
}

static void load(const char *file)
{
    int fd = open(file, BS_O_READ);

    copy_string(path, sizeof(path), file);
    caret = top_line = modified = 0;
    if (fd == -BS_ENOENT) {
        length = 0;
        copy_string(status, sizeof(status), "new file");
        return;
    }
    if (fd < 0) {
        length = 0;
        copy_string(status, sizeof(status), strerror(fd));
        return;
    }
    length = read(fd, text, TEXT_MAX - 1);
    close(fd);
    if (length < 0) {
        copy_string(status, sizeof(status), strerror(length));
        length = 0;
    } else {
        copy_string(status, sizeof(status), length == TEXT_MAX - 1 ? "too long: only the start is shown" : "");
    }
}

static int save(void)
{
    int fd = open(path, BS_O_WRITE), written;

    if (fd < 0) {
        copy_string(status, sizeof(status), strerror(fd));
        return fd;
    }
    written = write(fd, text, (unsigned int)length);
    close(fd);
    if (written == length) {
        modified = 0;
        copy_string(status, sizeof(status), "saved");
        return 0;
    }
    copy_string(status, sizeof(status), "save failed");
    return -BS_EIO;
}

static int notes_unsaved(void)
{
    return modified;
}

static void notes_open_request(const char *file)
{
    if (!file && !path[0])
        default_path();
    if (file && (strcmp(file, path) != 0 || !modified))
        load(file);
}

static void keep_caret_visible(void)
{
    int line = caret_line();

    if (line < top_line)
        top_line = line;
    if (line >= top_line + visible_lines)
        top_line = line - visible_lines + 1;
}

static void notes_draw(int w, int h, int focused)
{
    int line = 0, column = 0;

    visible_lines = (h - BAR_H - PAD * 2) / LINE_H;
    visible_columns = (w - PAD * 2) / CHAR_W;

    gfx_gradient(0, 0, w, BAR_H, LIGHTER(theme->window, 120), DARKER(theme->window, 30));
    gfx_button(4, 3, 36, 14, "Save", modified);
    gfx_button(44, 3, 36, 14, "New", 0);
    gfx_text_n(86, 7, path, (w - 92) / CHAR_W - 2, theme->text);
    if (modified)
        gfx_text(w - 10, 7, "*", app_notes.accent);
    gfx_fill(0, BAR_H, w, 2, theme->line);
    gfx_fill(0, BAR_H + 2, w, h - BAR_H - 2, theme->field);
    for (int rule = BAR_H + PAD + LINE_H - 1; rule < h; rule += LINE_H) /* the rules sit under each line */
        gfx_fill(0, rule, w, 1, theme->rule);
    gfx_fill(PAD - 3, BAR_H + 2, 1, h - BAR_H - 2, mix(theme->field, 0xE05050, 110)); /* the margin */

    for (int i = 0; i <= length; i++) {
        int on_screen = line >= top_line && line < top_line + visible_lines;
        int x = PAD + column * CHAR_W, y = BAR_H + PAD + (line - top_line) * LINE_H;

        if (i == caret && on_screen && focused && wm_frame() / 25 % 2 == 0)
            gfx_fill(x - 1, y - 1, 1, 9, theme->text);
        if (i == length)
            break;
        if (text[i] == '\n') {
            line++;
            column = 0;
            continue;
        }
        if (on_screen && column < visible_columns) {
            char c[2] = {text[i], 0};

            gfx_text(x, y, c, theme->text);
        }
        column++;
    }
    if (status[0])
        gfx_text(w - (int)strlen(status) * CHAR_W - 8, h - 12, status, theme->dim);
}

static void insert(char c)
{
    if (length >= TEXT_MAX - 1)
        return;
    for (int i = length; i > caret; i--)
        text[i] = text[i - 1];
    text[caret++] = c;
    length++;
    modified = 1;
    status[0] = 0;
}

static void delete_at(int position)
{
    for (int i = position; i < length - 1; i++)
        text[i] = text[i + 1];
    length--;
    modified = 1;
    status[0] = 0;
}

static void move_vertical(int direction)
{
    int start = line_start(caret), column = caret - start, target;

    if (direction < 0) {
        if (start == 0)
            return;
        target = line_start(start - 1);
    } else {
        if (line_end(caret) >= length)
            return;
        target = line_end(caret) + 1;
    }
    caret = target + column;
    if (caret > line_end(target))
        caret = line_end(target);
}

static void notes_key(int key)
{
    switch (key) {
    case CTRL('s'): save(); break;
    case '\b': if (caret > 0) delete_at(--caret); break;
    case BS_KEY_DEL: if (caret < length) delete_at(caret); break;
    case BS_KEY_LEFT: if (caret > 0) caret--; break;
    case BS_KEY_RIGHT: if (caret < length) caret++; break;
    case BS_KEY_UP: move_vertical(-1); break;
    case BS_KEY_DOWN: move_vertical(1); break;
    case BS_KEY_HOME: caret = line_start(caret); break;
    case BS_KEY_END: caret = line_end(caret); break;
    case BS_KEY_PGUP: for (int i = 0; i < visible_lines; i++) move_vertical(-1); break;
    case BS_KEY_PGDN: for (int i = 0; i < visible_lines; i++) move_vertical(1); break;
    case '\t': for (int i = 0; i < 4; i++) insert(' '); break;
    case '\n': insert('\n'); break;
    default: if (key >= 0x20 && key < 0x7F) insert((char)key); break;
    }
    keep_caret_visible();
}

static void notes_press(int x, int y, int w, int h)
{
    int want_line, want_column, line = 0, position = 0;

    (void)w;
    (void)h;
    if (gfx_inside(x, y, 4, 3, 36, 14)) {
        save();
        return;
    }
    if (gfx_inside(x, y, 44, 3, 36, 14)) {
        length = caret = top_line = modified = 0;
        default_path();
        copy_string(status, sizeof(status), "new file");
        return;
    }
    if (y < BAR_H + PAD)
        return;
    want_line = top_line + (y - BAR_H - PAD) / LINE_H;
    want_column = (x - PAD + CHAR_W / 2) / CHAR_W;
    while (line < want_line && line_end(position) < length) {
        position = line_end(position) + 1;
        line++;
    }
    caret = position + want_column;
    if (caret > line_end(position))
        caret = line_end(position);
}

const struct app app_notes = {
    .name = "BSnote", .blurb = "edit text", .accent = 0x3F78D8, .icon = ICON_NOTES,
    .unsaved = notes_unsaved, .save = save,
    .default_w = 380, .default_h = 260, .min_w = 180, .min_h = 90,
    .open = notes_open_request, .draw = notes_draw, .key = notes_key, .press = notes_press,
};
