/* Files: browse folders and open files with whatever handles them. */
#include "bs.h"
#include "bdeskui.h"

#define MAX_ENTRIES 160
#define ROW_H 18
#define BAR_H 20
#define DOUBLE_CLICK_FRAMES 20

static struct bs_dirent entries[MAX_ENTRIES];
static int entry_count, selected = -1, scroll, last_click_frame = -100, last_click_row = -1;
static char path[BS_PATH_MAX];
static char status[64];

static int load(const char *new_path)
{
    static struct bs_dirent scratch[MAX_ENTRIES];
    int fd = open(new_path, BS_O_READ), count = 0, rc = 0;

    if (fd < 0)
        return fd;
    while (count < MAX_ENTRIES && (rc = readdir(fd, &scratch[count])) > 0)
        count++;
    close(fd);
    if (rc < 0 && count == 0)
        return rc;
    memcpy(entries, scratch, sizeof(entries));
    entry_count = count;
    selected = -1;
    scroll = 0;
    if (new_path != path)
        copy_string(path, sizeof(path), new_path);
    status[0] = 0;
    return 0;
}

static void child_path(const char *name, char *out)
{
    copy_string(out, BS_PATH_MAX, path);
    if (strlen(path) > 1)
        copy_string(out + strlen(out), BS_PATH_MAX - strlen(out), "/");
    copy_string(out + strlen(out), BS_PATH_MAX - strlen(out), name);
}

static void go_up(void)
{
    char parent[BS_PATH_MAX];
    int end = (int)strlen(path);

    copy_string(parent, sizeof(parent), path);
    while (end > 1 && parent[--end] != '/')
        ;
    parent[end > 0 ? end : 1] = 0;
    load(parent);
}

static void open_entry(int index)
{
    char target[BS_PATH_MAX];
    const struct bs_dirent *e = &entries[index];

    child_path(e->name, target);
    if (e->type != BS_TYPE_FILE) {
        int rc = load(target);

        if (rc < 0)
            copy_string(status, sizeof(status), strerror(rc));
    } else if (ends_with(e->name, ".bs")) {
        terminal_run(target);
    } else if (ends_with(e->name, ".html") || ends_with(e->name, ".htm")) {
        char url[BS_PATH_MAX + 8], *argv[2];

        copy_string(url, sizeof(url), "file:");
        copy_string(url + 5, sizeof(url) - 5, target);
        argv[0] = "web";
        argv[1] = url;
        bdeskui_run_program(2, argv);
    } else {
        notes_open(target);
    }
}

static void files_open(const char *arg)
{
    if (load(arg ? arg : path[0] ? path : bdeskui_home()) < 0)
        load("/");
}

static int visible_rows(int h)
{
    return (h - BAR_H - 4) / ROW_H;
}

static void files_draw(int w, int h, int focused)
{
    int rows = visible_rows(h);

    (void)focused;
    gfx_gradient(0, 0, w, BAR_H, LIGHTER(theme->window, 120), DARKER(theme->window, 30));
    gfx_button(4, 3, 30, 14, "Up", 0);
    gfx_button(38, 3, 40, 14, "Open", 0);
    gfx_inset(85, 4, w - 92, 12, theme->field);
    gfx_text_n(88, 7, path, (w - 98) / CHAR_W, theme->text);
    gfx_fill(0, BAR_H, w, 2, theme->line);

    for (int i = 0; i < rows && scroll + i < entry_count; i++) {
        const struct bs_dirent *e = &entries[scroll + i];
        int y = BAR_H + 3 + i * ROW_H;
        enum icon icon = e->type == BS_TYPE_DIR || e->type == BS_TYPE_SYMLINK ? ICON_FOLDER
                         : ends_with(e->name, ".bs")                         ? ICON_PROGRAM
                                                                             : ICON_FILE;

        if (scroll + i == selected)
            gfx_gel(2, y - 1, w - 4, ROW_H, theme->select, 0);
        gfx_icon(6, y, icon, 1);
        gfx_text_n(26, y + 5, e->name, (w - 108) / CHAR_W, theme->text);
        y += 5;
        if (e->type == BS_TYPE_FILE) {
            char size[16];
            unsigned int v = e->size, n = 15;

            size[n] = 0;
            do {
                size[--n] = (char)('0' + v % 10);
                v /= 10;
            } while (v && n);
            gfx_text(w - (int)(15 - n) * CHAR_W - 8, y, size + n, theme->dim);
        } else {
            gfx_text(w - 8 * CHAR_W, y, e->type == BS_TYPE_DIR ? "folder" : "link", theme->dim);
        }
    }
    if (entry_count == 0)
        gfx_text(18, BAR_H + 6, "(empty)", theme->dim);
    if (status[0])
        gfx_text(8, h - 12, status, 0xE0564A);
}

static void files_press(int x, int y, int w, int h)
{
    int row;

    (void)w;
    if (gfx_inside(x, y, 4, 3, 30, 14)) {
        go_up();
        return;
    }
    if (gfx_inside(x, y, 38, 3, 40, 14)) {
        if (selected >= 0)
            open_entry(selected);
        return;
    }
    if (y < BAR_H + 3)
        return;
    row = (y - BAR_H - 3) / ROW_H;
    if (row >= visible_rows(h) || scroll + row >= entry_count)
        return;
    row += scroll;
    if (row == last_click_row && wm_frame() - last_click_frame < DOUBLE_CLICK_FRAMES) {
        last_click_row = -1;
        open_entry(row);
        return;
    }
    selected = row;
    last_click_row = row;
    last_click_frame = wm_frame();
}

static void files_key(int key)
{
    if (key == BS_KEY_DOWN && selected + 1 < entry_count) {
        selected++;
    } else if (key == BS_KEY_UP && selected > 0) {
        selected--;
    } else if (key == '\n' && selected >= 0) {
        open_entry(selected);
        return;
    } else if (key == '\b') {
        go_up();
        return;
    }
    if (selected >= 0 && selected < scroll)
        scroll = selected;
    if (selected >= scroll + 12)
        scroll = selected - 11;
}

const struct app app_files = {
    .name = "BSfile", .blurb = "browse files", .accent = 0xE08A1E, .icon = ICON_FILES,
    .default_w = 330, .default_h = 260, .min_w = 200, .min_h = 100,
    .open = files_open, .draw = files_draw, .key = files_key, .press = files_press,
};
