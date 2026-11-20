/*
 * The window manager: where windows, the taskbar, the launcher menu, desktop
 * icons and dialogs go, and what clicks and keys do. How they look is the
 * skin's business (skins.c); the metrics come from the skin too.
 */
#include "bdeskui.h"

#define MAX_WINDOWS 10
#define GRIP 9
#define CASCADE_STEP 20
#define TASK_NAME_MAX 96
#define MENU_W 236
#define MENU_ROW 20
#define RUN_H 20
#define POWER_H 20
#define DOUBLE_CLICK_FRAMES 20
#define COMMAND_MAX 128

struct window {
    const struct app *app;
    int instance;
    int x, y, w, h; /* the content area */
    int minimized, maximized, discarded;
    int saved_x, saved_y, saved_w, saved_h;
};

struct launcher {
    const char *label, *blurb;
    enum icon icon;
    const struct app *app; /* a built-in app, or: */
    const char *command;   /* a program to launch (windowed or in a terminal) */
};

static const struct launcher launchers[] = {
    {"BSfile", "browse and open files", ICON_FILES, &app_files, 0},
    {"BSterm", "bsh in a window", ICON_TERMINAL, 0, ""},
    {"BSnote", "write and edit text", ICON_NOTES, &app_notes, 0},
    {"BSweb", "the web, windowed", ICON_WEB, 0, "web"},
    {"BSsys", "memory, disk, network", ICON_SYSTEM, &app_system, 0},
    {"BSconf", "skins, mouse, login", ICON_SETTINGS, &app_settings, 0},
    {"BScube", "spins, in a terminal", ICON_CUBE, 0, "cube"},
};
#define LAUNCHER_COUNT ((int)(sizeof(launchers) / sizeof(launchers[0])))

enum action { ACTION_NONE, ACTION_POWEROFF, ACTION_RESTART, ACTION_LOGOUT, ACTION_TTY, ACTION_SHUTDOWN };
static const struct {
    const char *label;
    enum action action;
} power_buttons[] = {
    {"shutdown", ACTION_POWEROFF},
    {"restart", ACTION_RESTART},
    {"log out", ACTION_LOGOUT},
    {"TTY", ACTION_TTY},
};
#define POWER_COUNT 4

/* Bottom of the array is drawn first; the last window has the focus. */
static struct window windows[MAX_WINDOWS];
static int window_count, cascade;
static enum { DRAG_NONE, DRAG_MOVE, DRAG_RESIZE, DRAG_CONTENT } drag;
static int drag_dx, drag_dy;
static unsigned int last_buttons;
static int frame_counter, exit_code = -1;
static int menu_open, menu_selected, icon_selected = -1;
static char run_command[COMMAND_MAX];
static int run_length;
static char clock_text[6] = "--:--", address_text[20] = "offline";
static int last_minute = -1;
static int last_click_frame = -100, last_click_x, last_click_y;

static struct {
    int open, hot, count;
    char title[32], message[80];
    const char *buttons[3];
    void (*choose)(int index);
} dialog;
static enum action leaving;
static unsigned int shutdown_started_at;
static int closing = -1;

int wm_frame(void)
{
    return frame_counter;
}

int wm_exit_code(void)
{
    return exit_code;
}

void wm_reset(void)
{
    window_count = cascade = 0;
    exit_code = -1;
    menu_open = 0;
    icon_selected = -1;
    leaving = ACTION_NONE;
    dialog.open = 0;
}

static int work_top(void)
{
    return skin->taskbar_top ? skin->taskbar_h : 0;
}

static int work_bottom(void)
{
    return skin->taskbar_top ? SCREEN_H : SCREEN_H - skin->taskbar_h;
}

static int bar_y(void)
{
    return skin->taskbar_top ? 0 : SCREEN_H - skin->taskbar_h;
}

static struct window *focused(void)
{
    if (window_count == 0 || windows[window_count - 1].minimized)
        return 0;
    return &windows[window_count - 1];
}

static int find(const struct app *app, int instance)
{
    for (int i = 0; i < window_count; i++)
        if (windows[i].app == app && windows[i].instance == instance)
            return i;
    return -1;
}

static void raise_window(int index)
{
    struct window top = windows[index];

    for (int i = index; i < window_count - 1; i++)
        windows[i] = windows[i + 1];
    windows[window_count - 1] = top;
    windows[window_count - 1].minimized = 0;
}

static const char *title_of(const struct window *w)
{
    return w->app->title ? w->app->title(w->instance) : w->app->name;
}

/* ---- Geometry, from the skin's metrics ------------------------------------------ */

static int top_band(void) /* from the window's top edge to its content */
{
    return skin->frame + skin->title_h + skin->gap;
}

static void geometry(const struct window *w, struct frame_info *f)
{
    f->x = w->x - skin->frame;
    f->y = w->y - top_band();
    f->w = w->w + 2 * skin->frame;
    f->h = w->h + top_band() + skin->frame;
    f->title_x = w->x;
    f->title_y = w->y - skin->gap - skin->title_h;
    f->title_w = w->w;
    f->content_x = w->x;
    f->content_y = w->y;
    f->content_w = w->w;
    f->content_h = w->h;
    for (int i = 0; i < 3; i++)
        f->button_x[i] = w->x + w->w - (3 - i) * skin->title_button_w;
    f->title = title_of(w);
    f->icon = w->app->icon;
    f->focused = 0;
}

static void clamp(struct window *w)
{
    int top_min = work_top() + top_band();

    if (w->w > SCREEN_W - 2 * skin->frame)
        w->w = SCREEN_W - 2 * skin->frame;
    if (w->h > work_bottom() - top_min - skin->frame)
        w->h = work_bottom() - top_min - skin->frame;
    if (w->x < skin->frame)
        w->x = skin->frame;
    if (w->y < top_min)
        w->y = top_min;
    if (w->x + w->w > SCREEN_W - skin->frame)
        w->x = SCREEN_W - skin->frame - w->w;
    if (w->y > work_bottom() - 24)
        w->y = work_bottom() - 24;
}

/* All of it on screen, above the taskbar: new windows, and before a program draws in one. */
static void fit(struct window *w)
{
    clamp(w);
    if (w->y + w->h > work_bottom() - skin->frame)
        w->y = work_bottom() - skin->frame - w->h;
    clamp(w);
}

int wm_open(const struct app *app, int instance, const char *arg)
{
    int index = find(app, instance);

    menu_open = 0;
    if (index >= 0) {
        raise_window(index);
    } else if (window_count < MAX_WINDOWS) {
        struct window *w = &windows[window_count++];

        memset(w, 0, sizeof(*w));
        w->app = app;
        w->instance = instance;
        w->w = app->default_w;
        w->h = app->default_h;
        w->x = SCREEN_W / 4 + cascade * CASCADE_STEP;
        w->y = work_top() + top_band() + 16 + cascade * CASCADE_STEP;
        cascade = (cascade + 1) % 6;
        fit(w);
    } else {
        return -1;
    }
    if (app->open)
        app->open(instance, arg);
    return window_count - 1;
}

static void close_window(int index)
{
    const struct app *app = windows[index].app;
    int instance = windows[index].instance;

    for (int i = index; i < window_count - 1; i++)
        windows[i] = windows[i + 1];
    window_count--;
    if (app->closed)
        app->closed(instance);
}

void wm_close_instance(const struct app *app, int instance)
{
    int index = find(app, instance);

    if (index >= 0)
        close_window(index);
}

int wm_content_rect(const struct app *app, int instance, int *sx, int *sy, int *sw, int *sh)
{
    int index = find(app, instance);

    if (index < 0)
        return -1;
    fit(&windows[index]);
    gfx_screen_rect(windows[index].x, windows[index].y, windows[index].w, windows[index].h, sx, sy, sw, sh);
    return 0;
}

static void toggle_maximize(struct window *w)
{
    if (w->maximized) {
        w->x = w->saved_x;
        w->y = w->saved_y;
        w->w = w->saved_w;
        w->h = w->saved_h;
        w->maximized = 0;
    } else {
        w->saved_x = w->x;
        w->saved_y = w->y;
        w->saved_w = w->w;
        w->saved_h = w->h;
        w->x = skin->frame;
        w->y = work_top() + top_band();
        w->w = SCREEN_W - 2 * skin->frame;
        w->h = work_bottom() - w->y - skin->frame;
        w->maximized = 1;
    }
}

/* ---- Dialogs and leaving ------------------------------------------------------- */

static void ask(const char *title, const char *message, const char *a, const char *b, const char *c,
                void (*choose)(int))
{
    memset(&dialog, 0, sizeof(dialog));
    copy_string(dialog.title, sizeof(dialog.title), title);
    copy_string(dialog.message, sizeof(dialog.message), message);
    dialog.buttons[0] = a;
    dialog.buttons[1] = b;
    dialog.buttons[2] = c;
    dialog.count = c ? 3 : b ? 2 : 1;
    dialog.choose = choose;
    dialog.open = 1;
}

static void continue_leaving(void);

static void unsaved_message(int index, char *out, unsigned int size)
{
    copy_string(out, size, title_of(&windows[index]));
    copy_string(out + strlen(out), size - strlen(out), " has unsaved changes.");
}

static int unsaved_window(void)
{
    for (int i = 0; i < window_count; i++)
        if (!windows[i].discarded && windows[i].app->unsaved && windows[i].app->unsaved())
            return i;
    return -1;
}

static void save_failed_answer(int choice)
{
    if (choice == 0)
        continue_leaving();
    else
        leaving = ACTION_NONE;
}

static void unsaved_answer(int choice)
{
    int index = unsaved_window();

    if (index < 0)
        return;
    if (choice == 0 && windows[index].app->save) {
        if (windows[index].app->save() < 0) {
            char message[80];

            copy_string(message, sizeof(message), title_of(&windows[index]));
            copy_string(message + strlen(message), sizeof(message) - strlen(message), " could not save. leave anyway?");
            windows[index].discarded = 1;
            ask("Save failed", message, "leave", leaving == ACTION_SHUTDOWN ? 0 : "stay", 0, save_failed_answer);
            return;
        }
    } else if (choice == 1) {
        windows[index].discarded = 1;
    } else {
        leaving = ACTION_NONE;
        return;
    }
    continue_leaving();
}

static void continue_leaving(void)
{
    int index = unsaved_window();

    if (index >= 0) {
        char message[80];

        unsaved_message(index, message, sizeof(message));
        ask("Unsaved work", message, "save", "discard", leaving == ACTION_SHUTDOWN ? 0 : "cancel", unsaved_answer);
        return;
    }
    switch (leaving) {
    case ACTION_POWEROFF:
        shutdown(BS_SHUTDOWN_POWEROFF);
        exit_code = EXIT_LOGOUT;
        break;
    case ACTION_RESTART:
        shutdown(BS_SHUTDOWN_REBOOT);
        exit_code = EXIT_LOGOUT;
        break;
    case ACTION_LOGOUT:
    case ACTION_SHUTDOWN:
        exit_code = EXIT_LOGOUT;
        break;
    default:
        break;
    }
}

static void leave(enum action action)
{
    if (leaving == ACTION_SHUTDOWN)
        return;
    menu_open = 0;
    if (action == ACTION_TTY) { /* not leaving: bsh on the whole screen, then back */
        run_full_screen_shell();
        return;
    }
    leaving = action;
    for (int i = 0; i < window_count; i++)
        windows[i].discarded = 0;
    continue_leaving();
}

void wm_shutdown_started(void)
{
    struct bs_sysinfo info;

    sysinfo(&info);
    shutdown_started_at = info.uptime_seconds;
    leave(ACTION_SHUTDOWN);
}

static void close_answer(int choice)
{
    int index = closing;

    closing = -1;
    if (index < 0 || index >= window_count || choice == 2)
        return;
    if (choice == 0 && windows[index].app->save && windows[index].app->save() < 0)
        return;
    close_window(index);
}

static void request_close(int index)
{
    if (windows[index].app->unsaved && windows[index].app->unsaved()) {
        char message[80];

        closing = index;
        unsaved_message(index, message, sizeof(message));
        ask("Close", message, "save", "discard", "cancel", close_answer);
        return;
    }
    close_window(index);
}

static void dialog_key(int key)
{
    if (key == BS_KEY_LEFT && dialog.hot > 0)
        dialog.hot--;
    else if ((key == BS_KEY_RIGHT || key == '\t') && dialog.hot + 1 < dialog.count)
        dialog.hot++;
    else if (key == KEY_ESCAPE && dialog.count == 3)
        dialog.hot = 2;
    if (key == '\n' || (key == KEY_ESCAPE && dialog.count == 3)) {
        void (*choose)(int) = dialog.choose;

        dialog.open = 0;
        if (choose)
            choose(dialog.hot);
    }
}

#define DIALOG_W 300
#define DIALOG_H 104
#define DIALOG_BUTTON_W 66

static int dialog_x(void) { return (SCREEN_W - DIALOG_W) / 2; }
static int dialog_y(void) { return work_top() + (work_bottom() - work_top() - DIALOG_H) / 2; }

static int dialog_button_x(int i)
{
    int gap = 8, total = dialog.count * DIALOG_BUTTON_W + (dialog.count - 1) * gap;

    return dialog_x() + DIALOG_W - 14 - total + i * (DIALOG_BUTTON_W + gap);
}

static void dialog_press(int x, int y)
{
    for (int i = 0; i < dialog.count; i++) {
        if (gfx_inside(x, y, dialog_button_x(i), dialog_y() + DIALOG_H - 28, DIALOG_BUTTON_W, 18)) {
            dialog.hot = i;
            dialog_key('\n');
            return;
        }
    }
}

static void draw_dialog(void)
{
    int x = dialog_x(), y = dialog_y(), top = y + panel_title_h();

    gfx_unclip();
    gfx_darken(0, 0, SCREEN_W, SCREEN_H, 80); /* everything else waits */
    skin->draw_panel(x, y, DIALOG_W, DIALOG_H, dialog.title);
    gfx_icon(x + 12, top + 10, ICON_POWER, 2);
    gfx_text_n(x + 54, top + 14, dialog.message, (DIALOG_W - 64) / CHAR_W, skin->text);
    if (leaving == ACTION_SHUTDOWN) {
        struct bs_sysinfo info;
        char line[40] = "shutting down: ";
        unsigned int used, left, n = strlen(line);

        sysinfo(&info);
        used = info.uptime_seconds - shutdown_started_at;
        left = used < 15 ? 15 - used : 0;
        line[n++] = (char)('0' + left / 10);
        line[n++] = (char)('0' + left % 10);
        copy_string(line + n, sizeof(line) - n, " s left");
        gfx_text(x + 54, top + 28, line, skin->dim);
    }
    for (int i = 0; i < dialog.count; i++)
        skin->draw_button(dialog_button_x(i), y + DIALOG_H - 28, DIALOG_BUTTON_W, 18, dialog.buttons[i],
                          i == dialog.hot);
}

/* ---- The launcher menu ---------------------------------------------------------- */

static int menu_height(void)
{
    return panel_title_h() + 4 + LAUNCHER_COUNT * MENU_ROW + RUN_H + POWER_H + 10;
}

static int menu_top(void)
{
    return skin->taskbar_top ? skin->taskbar_h + 2 : work_bottom() - menu_height() - 2;
}

static void launch(int index)
{
    const struct launcher *l = &launchers[index];

    menu_open = 0;
    if (l->app)
        wm_open(l->app, 0, 0);
    else if (!l->command[0])
        terminal_new(0);
    else
        launch_command(l->command);
}

static void submit_run_field(void)
{
    char line[COMMAND_MAX];

    menu_open = 0;
    if (run_length == 0) {
        launch(menu_selected);
        return;
    }
    copy_string(line, sizeof(line), run_command);
    run_length = 0;
    run_command[0] = 0;
    launch_command(line);
}

static void menu_key(int key)
{
    if (key == KEY_ESCAPE) {
        menu_open = 0;
    } else if (key == BS_KEY_UP) {
        menu_selected = (menu_selected + LAUNCHER_COUNT - 1) % LAUNCHER_COUNT;
    } else if (key == BS_KEY_DOWN) {
        menu_selected = (menu_selected + 1) % LAUNCHER_COUNT;
    } else if (key == '\n') {
        submit_run_field();
    } else if (key == '\b') {
        if (run_length > 0)
            run_command[--run_length] = 0;
    } else if (key >= 0x20 && key < 0x7F && run_length + 1 < COMMAND_MAX) {
        run_command[run_length++] = (char)key;
        run_command[run_length] = 0;
    }
}

static int power_button_width(void)
{
    return (MENU_W - 12 - 3 * 4) / POWER_COUNT;
}

static int power_button_x(int i)
{
    return 2 + 6 + i * (power_button_width() + 4);
}

static int rows_top(void)
{
    return menu_top() + panel_title_h() + 2;
}

static int run_y(void)
{
    return rows_top() + LAUNCHER_COUNT * MENU_ROW + 2;
}

static int power_y(void)
{
    return run_y() + RUN_H + 2;
}

static void draw_menu(int mouse_x, int mouse_y)
{
    int top = menu_top(), x = 2, y;

    gfx_unclip();
    skin->draw_panel(x, top, MENU_W, menu_height(), bdeskui_user());
    for (int i = 0; i < LAUNCHER_COUNT; i++) {
        int row = rows_top() + i * MENU_ROW;
        int hot = i == menu_selected || gfx_inside(mouse_x, mouse_y, x, row, MENU_W, MENU_ROW);

        if (hot)
            gfx_fill(x + 4, row, MENU_W - 8, MENU_ROW - 1, skin->select);
        gfx_icon(x + 8, row + 2, launchers[i].icon, 1);
        gfx_text_bold(x + 30, row + 3, launchers[i].label, skin->text);
        gfx_text(x + 30 + 8 * (CHAR_W + 1), row + 3, launchers[i].blurb, hot ? skin->text : skin->dim);
    }
    y = run_y();
    gfx_text_bold(x + 8, y + 6, "run", skin->text);
    skin->draw_field(x + 38, y + 2, MENU_W - 46, RUN_H - 4, 1);
    gfx_text_n(x + 42, y + 6, run_command, (MENU_W - 54) / CHAR_W, skin->text);
    if (wm_frame() / 25 % 2 == 0)
        gfx_fill(x + 42 + run_length * CHAR_W, y + 5, 1, 9, skin->text);
    y = power_y();
    for (int i = 0; i < POWER_COUNT; i++)
        skin->draw_button(x + power_button_x(i) - 2, y, power_button_width(), POWER_H - 2, power_buttons[i].label,
                          gfx_inside(mouse_x, mouse_y, x + power_button_x(i) - 2, y, power_button_width(), POWER_H - 2));
}

static int menu_press(int x, int y)
{
    if (!gfx_inside(x, y, 2, menu_top(), MENU_W, menu_height()))
        return 0;
    if (y >= rows_top() && y < rows_top() + LAUNCHER_COUNT * MENU_ROW) {
        launch((y - rows_top()) / MENU_ROW);
        return 1;
    }
    for (int i = 0; i < POWER_COUNT; i++)
        if (gfx_inside(x, y, power_button_x(i), power_y(), power_button_width(), POWER_H - 2))
            leave(power_buttons[i].action);
    return 1;
}

/* ---- Desktop icons -------------------------------------------------------------- */

static int tile_size(void) { return skin->tile_size ? skin->tile_size : 36; }
static int pitch_x(void) { return tile_size() + 24; }
static int pitch_y(void) { return tile_size() + 22; }

static int icons_per_column(void)
{
    int rows = (work_bottom() - work_top() - 8) / pitch_y();

    return rows > 0 ? rows : 1;
}

static int icon_x(int i) { return 12 + i / icons_per_column() * pitch_x(); }
static int icon_y(int i) { return work_top() + 8 + i % icons_per_column() * pitch_y(); }

static int icon_at(int x, int y)
{
    for (int i = 0; i < LAUNCHER_COUNT; i++)
        if (gfx_inside(x, y, icon_x(i) - 6, icon_y(i), tile_size() + 12, pitch_y() - 4))
            return i;
    return -1;
}

static void draw_icons(void)
{
    gfx_unclip();
    for (int i = 0; i < LAUNCHER_COUNT; i++)
        skin->draw_tile(icon_x(i), icon_y(i), tile_size(), launchers[i].icon, launchers[i].label, i == icon_selected);
}

/* ---- The taskbar: the launcher, an entry per window, the tray -------------------- */

static int entry_width(int name_w)
{
    return name_w + skin->task_buttons * skin->title_button_w;
}

static int task_name_width(void)
{
    int room = SCREEN_W - 150 - (skin->launcher_w + 8);
    int each = window_count ? room / window_count - 4 - skin->task_buttons * skin->title_button_w : TASK_NAME_MAX;

    return each < TASK_NAME_MAX ? each : TASK_NAME_MAX;
}

static int task_x(int i)
{
    return skin->launcher_w + 8 + i * (entry_width(task_name_width()) + 4);
}

static void press_taskbar(int x)
{
    int name_w = task_name_width();

    if (x < skin->launcher_w + 4) {
        menu_open = !menu_open;
        menu_selected = 0;
        return;
    }
    for (int i = 0; i < window_count && i < TASK_MAX; i++) {
        int left = task_x(i);

        if (x >= left && x < left + name_w) {
            if (focused() == &windows[i])
                windows[i].minimized = 1;
            else
                raise_window(i);
            return;
        }
        if (skin->task_buttons && x >= left + name_w && x < left + name_w + skin->title_button_w) {
            raise_window(i);
            toggle_maximize(&windows[window_count - 1]);
            return;
        }
        if (skin->task_buttons && x >= left + name_w + skin->title_button_w && x < left + entry_width(name_w)) {
            request_close(i);
            return;
        }
    }
}

static void draw_taskbar(void)
{
    struct bar_info b;

    memset(&b, 0, sizeof(b));
    b.y = bar_y();
    b.width = SCREEN_W;
    b.menu_open = menu_open;
    b.clock = clock_text;
    b.address = address_text;
    for (int i = 0; i < window_count && i < TASK_MAX; i++) {
        b.entries[i].x = task_x(i);
        b.entries[i].name_w = task_name_width();
        b.entries[i].w = entry_width(b.entries[i].name_w);
        b.entries[i].name = title_of(&windows[i]);
        b.entries[i].icon = windows[i].app->icon;
        b.entries[i].active = focused() == &windows[i];
        b.entries[i].minimized = windows[i].minimized;
        b.count++;
    }
    gfx_unclip();
    skin->draw_taskbar(&b);
}

/* ---- Input -------------------------------------------------------------------- */

static int window_at(int x, int y)
{
    for (int i = window_count - 1; i >= 0; i--) {
        struct frame_info f;

        if (windows[i].minimized)
            continue;
        geometry(&windows[i], &f);
        if (gfx_inside(x, y, f.x, f.y, f.w, f.h))
            return i;
    }
    return -1;
}

static void press(int x, int y)
{
    int index;
    int double_click = wm_frame() - last_click_frame < DOUBLE_CLICK_FRAMES &&
                       x - last_click_x < 4 && last_click_x - x < 4 && y - last_click_y < 4 && last_click_y - y < 4;

    last_click_frame = wm_frame();
    last_click_x = x;
    last_click_y = y;

    if (dialog.open) {
        dialog_press(x, y);
        return;
    }
    if (y >= bar_y() && y < bar_y() + skin->taskbar_h) {
        press_taskbar(x);
        return;
    }
    if (menu_open) {
        if (menu_press(x, y))
            return;
        menu_open = 0;
    }

    index = window_at(x, y);
    if (index < 0) {
        int icon = icon_at(x, y);

        if (icon >= 0 && double_click && icon == icon_selected)
            launch(icon);
        icon_selected = icon;
        return;
    }
    icon_selected = -1;
    raise_window(index);
    {
        struct window *w = &windows[window_count - 1];
        struct frame_info f;

        geometry(w, &f);
        if (y < w->y) { /* the band above the content: title bar and buttons */
            if (gfx_inside(x, y, f.button_x[0], f.y, skin->title_button_w, w->y - f.y)) {
                w->minimized = 1;
            } else if (gfx_inside(x, y, f.button_x[1], f.y, skin->title_button_w, w->y - f.y)) {
                toggle_maximize(w);
            } else if (gfx_inside(x, y, f.button_x[2], f.y, skin->title_button_w, w->y - f.y)) {
                request_close(window_count - 1);
            } else if (double_click) {
                toggle_maximize(w);
            } else {
                drag = DRAG_MOVE;
                drag_dx = x - w->x;
                drag_dy = y - w->y;
            }
        } else if (gfx_inside(x, y, w->x + w->w - GRIP, w->y + w->h - GRIP, GRIP + skin->frame, GRIP + skin->frame)) {
            drag = DRAG_RESIZE;
            drag_dx = w->x + w->w - x;
            drag_dy = w->y + w->h - y;
        } else if (gfx_inside(x, y, w->x, w->y, w->w, w->h) && w->app->press) {
            drag = DRAG_CONTENT;
            w->app->press(w->instance, x - w->x, y - w->y, w->w, w->h);
        }
    }
}

void wm_mouse(int x, int y, unsigned int buttons)
{
    int pressed = (buttons & BS_MOUSE_LEFT) && !(last_buttons & BS_MOUSE_LEFT);
    struct window *w = focused();

    last_buttons = buttons;
    if (pressed) {
        press(x, y);
        return;
    }
    if (!(buttons & BS_MOUSE_LEFT)) {
        drag = DRAG_NONE;
        return;
    }
    if (!w || dialog.open)
        return;

    if (drag == DRAG_MOVE) {
        w->x = x - drag_dx;
        w->y = y - drag_dy;
        w->maximized = 0;
        clamp(w);
    } else if (drag == DRAG_RESIZE) {
        w->w = x + drag_dx - w->x;
        w->h = y + drag_dy - w->y;
        if (w->w < w->app->min_w)
            w->w = w->app->min_w;
        if (w->h < w->app->min_h)
            w->h = w->app->min_h;
        w->maximized = 0;
        clamp(w);
    } else if (drag == DRAG_CONTENT && w->app->drag) {
        w->app->drag(w->instance, x - w->x, y - w->y, w->w, w->h);
    }
}

void wm_key(int key)
{
    struct window *w = focused();

    if (dialog.open) {
        dialog_key(key);
        return;
    }
    if (key == CTRL('b')) {
        menu_open = !menu_open;
        menu_selected = 0;
        return;
    }
    if (menu_open) {
        menu_key(key);
        return;
    }
    if (key == CTRL('w')) {
        if (w)
            request_close(window_count - 1);
    } else if (key == CTRL('n')) {
        if (window_count > 1)
            raise_window(0);
        else if (window_count == 1)
            windows[0].minimized = 0;
    } else if (key == CTRL('t')) {
        terminal_new(0);
    } else if (w && w->app->key) {
        w->app->key(w->instance, key);
    }
}

static void update_tray(void)
{
    struct bs_time t;
    struct bs_netinfo net;

    get_time(&t);
    if ((int)t.minute == last_minute)
        return;
    last_minute = (int)t.minute;
    clock_text[0] = (char)('0' + t.hour / 10);
    clock_text[1] = (char)('0' + t.hour % 10);
    clock_text[2] = ':';
    clock_text[3] = (char)('0' + t.minute / 10);
    clock_text[4] = (char)('0' + t.minute % 10);

    net_info(&net);
    if (net.ip) {
        char *p = address_text;
        unsigned int parts[4] = {net.ip >> 24, (net.ip >> 16) & 255, (net.ip >> 8) & 255, net.ip & 255};

        for (int i = 0; i < 4; i++) {
            unsigned int v = parts[i];

            if (v >= 100)
                *p++ = (char)('0' + v / 100);
            if (v >= 10)
                *p++ = (char)('0' + v / 10 % 10);
            *p++ = (char)('0' + v % 10);
            if (i < 3)
                *p++ = '.';
        }
        *p = 0;
    } else {
        copy_string(address_text, sizeof(address_text), net.present ? "no address" : "no network");
    }
}

int wm_tick(void)
{
    int redraw = 0;

    frame_counter++;
    update_tray();
    for (int i = 0; i < window_count; i++)
        if (!windows[i].minimized && windows[i].app->tick && windows[i].app->tick())
            redraw = 1;
    if (frame_counter % 25 == 0)
        redraw = 1; /* caret blink, the shutdown countdown */
    return redraw;
}

/* ---- Drawing -------------------------------------------------------------------- */

static void draw_window(const struct window *w, int is_focused)
{
    struct frame_info f;

    geometry(w, &f);
    f.focused = is_focused;
    gfx_unclip();
    skin->draw_window(&f);
    gfx_clip(w->x, w->y, w->w, w->h);
    gfx_fill(0, 0, w->w, w->h, skin->window);
    w->app->draw(w->instance, w->w, w->h, is_focused);
    for (int ridge = 0; ridge < 3; ridge++) { /* the resize grip */
        int reach = 3 + ridge * 3;

        for (int i = 0; i < reach; i++)
            gfx_fill(w->w - 2 - reach + i, w->h - 2 - i, 1, 1, skin->dim);
    }
    gfx_unclip();
}

void wm_render(int mouse_x, int mouse_y, int show_pointer)
{
    enum pointer_kind pointer = POINTER_ARROW;
    struct window *w = focused();

    gfx_background();
    draw_icons();
    for (int i = 0; i < window_count; i++)
        if (!windows[i].minimized)
            draw_window(&windows[i], &windows[i] == focused());
    draw_taskbar();
    if (menu_open)
        draw_menu(mouse_x, mouse_y);
    if (dialog.open)
        draw_dialog();
    if (show_pointer) {
        if (drag == DRAG_MOVE)
            pointer = POINTER_MOVE;
        else if (drag == DRAG_RESIZE || (w && !dialog.open &&
                                         gfx_inside(mouse_x, mouse_y, w->x + w->w - GRIP, w->y + w->h - GRIP,
                                                    GRIP + skin->frame, GRIP + skin->frame)))
            pointer = POINTER_RESIZE;
        gfx_pointer(mouse_x, mouse_y, pointer);
    }
    gfx_present();
}
