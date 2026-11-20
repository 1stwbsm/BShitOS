/*
 * The window manager. Windows overlap and can be moved, resized, minimised,
 * maximised and closed. Desktop icons and the launch menu (the B) start apps;
 * the taskbar has a button per window, each with its own maximise and close.
 */
#include "bdeskui.h"

#define MAX_WINDOWS 8
#define FRAME 6         /* outline (2) plus a chrome margin around the content */
#define TITLE_H 24
#define SEP 2           /* the rule under the title bar */
#define TITLE_BUTTON 24 /* V, ^ and X segments, each the full title bar tall */
#define GRIP 10
#define CASCADE_STEP 22
#define B_SIZE 32
#define TASK_NAME_MAX 92
#define TASK_BUTTON 22
#define TRAY_W 120
#define MENU_W 260
#define MENU_ROW 24
#define MENU_HEAD 30
#define RUN_H 22
#define POWER_H 24
#define TILE 48
#define ICON_PITCH_X 70
#define ICON_PITCH_Y 66
#define DOUBLE_CLICK_FRAMES 20
#define COMMAND_MAX 128

struct window {
    const struct app *app;
    int x, y, w, h; /* the content area */
    int minimized, maximized, discarded;
    int saved_x, saved_y, saved_w, saved_h;
};

struct launcher {
    const char *label, *blurb;
    enum icon icon;
    const struct app *app;
    const char *program;
};

static const struct launcher launchers[] = {
    {"BSfile", "browse and open files", ICON_FILES, &app_files, 0},
    {"BSterm", "run programs with bsh", ICON_TERMINAL, &app_terminal, 0},
    {"BSnote", "write and edit text", ICON_NOTES, &app_notes, 0},
    {"BSweb", "browse (takes the screen)", ICON_WEB, 0, "web"},
    {"BSsys", "memory, disk, network", ICON_SYSTEM, &app_system, 0},
    {"BSconf", "skin, mouse, login", ICON_SETTINGS, &app_settings, 0},
    {"BScube", "spins (takes the screen)", ICON_CUBE, 0, "cube"},
};
#define LAUNCHER_COUNT ((int)(sizeof(launchers) / sizeof(launchers[0])))

enum action { ACTION_NONE, ACTION_POWEROFF, ACTION_RESTART, ACTION_LOGOUT, ACTION_TTY, ACTION_SHUTDOWN };
static const struct {
    const char *label;
    enum action action;
    unsigned int color;
} power_buttons[] = {
    {"shutdown", ACTION_POWEROFF, 0xC8382A},
    {"restart", ACTION_RESTART, 0xD88A20},
    {"log out", ACTION_LOGOUT, 0x4C80B8},
    {"to TTY", ACTION_TTY, 0x6A747C},
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

/* One modal question at a time. */
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

static int work_bottom(void)
{
    return SCREEN_H - TASKBAR_H;
}

static struct window *focused(void)
{
    if (window_count == 0 || windows[window_count - 1].minimized)
        return 0;
    return &windows[window_count - 1];
}

static int find(const struct app *app)
{
    for (int i = 0; i < window_count; i++)
        if (windows[i].app == app)
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

/* ---- Window geometry: a framed box, the title bar on top, a rule, the content -- */

static int outer_left(const struct window *w) { return w->x - FRAME; }
static int outer_top(const struct window *w) { return w->y - SEP - TITLE_H - 2; }
static int outer_width(const struct window *w) { return w->w + 2 * FRAME; }
static int outer_height(const struct window *w) { return w->h + TITLE_H + SEP + 2 + FRAME; }
static int title_top(const struct window *w) { return w->y - SEP - TITLE_H; }
static int title_left(const struct window *w) { return w->x - FRAME + 2; }
static int title_width(const struct window *w) { return w->w + 2 * FRAME - 4; }

/* 0 minimise (V), 1 maximise (^), 2 close (X) */
static int button_x(const struct window *w, int which)
{
    return title_left(w) + title_width(w) - (3 - which) * TITLE_BUTTON;
}

static void clamp(struct window *w)
{
    int top_min = 2 + TITLE_H + SEP;

    if (w->w > SCREEN_W - 2 * FRAME)
        w->w = SCREEN_W - 2 * FRAME;
    if (w->h > work_bottom() - top_min - FRAME)
        w->h = work_bottom() - top_min - FRAME;
    if (w->x < FRAME)
        w->x = FRAME;
    if (w->y < top_min)
        w->y = top_min;
    if (w->x + w->w > SCREEN_W - FRAME)
        w->x = SCREEN_W - FRAME - w->w;
    if (w->y > work_bottom() - 24)
        w->y = work_bottom() - 24;
}

void wm_open(const struct app *app, const char *arg)
{
    int index = find(app);

    menu_open = 0;
    if (index >= 0) {
        raise_window(index);
    } else if (window_count < MAX_WINDOWS) {
        struct window *w = &windows[window_count++];

        memset(w, 0, sizeof(*w));
        w->app = app;
        w->w = app->default_w;
        w->h = app->default_h;
        w->x = SCREEN_W / 3 + cascade * CASCADE_STEP - 60;
        w->y = 2 + TITLE_H + SEP + 18 + cascade * CASCADE_STEP;
        cascade = (cascade + 1) % 6;
        clamp(w);
    } else {
        return;
    }
    if (app->open)
        app->open(arg);
}

static void close_window(int index)
{
    for (int i = index; i < window_count - 1; i++)
        windows[i] = windows[i + 1];
    window_count--;
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
        w->x = FRAME;
        w->y = 2 + TITLE_H + SEP;
        w->w = SCREEN_W - 2 * FRAME;
        w->h = work_bottom() - w->y - FRAME;
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
    copy_string(out, size, windows[index].app->name);
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
        continue_leaving(); /* leave anyway: that window is marked discarded */
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

            copy_string(message, sizeof(message), windows[index].app->name);
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
    case ACTION_TTY:
        exit_code = EXIT_TO_TTY;
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
    leaving = action;
    menu_open = 0;
    for (int i = 0; i < window_count; i++)
        windows[i].discarded = 0;
    continue_leaving();
}

void wm_leave(int code)
{
    leave(code == EXIT_TO_TTY ? ACTION_TTY : ACTION_LOGOUT);
}

void wm_shutdown_started(void)
{
    struct bs_sysinfo info;

    sysinfo(&info);
    shutdown_started_at = info.uptime_seconds;
    leave(ACTION_SHUTDOWN);
}

/* Closing one window that has unsaved work. */
static void close_answer(int choice)
{
    int index = closing;

    closing = -1;
    if (index < 0 || index >= window_count || choice == 2)
        return;
    if (choice == 0 && windows[index].app->save && windows[index].app->save() < 0)
        return; /* the app shows why it could not save; the window stays */
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

#define DIALOG_W 320
#define DIALOG_H 120

static int dialog_x(void) { return (SCREEN_W - DIALOG_W) / 2; }
static int dialog_y(void) { return (work_bottom() - DIALOG_H) / 2; }

static int dialog_button_x(int i)
{
    int width = 72, gap = 8, total = dialog.count * width + (dialog.count - 1) * gap;

    return dialog_x() + DIALOG_W - 14 - total + i * (width + gap);
}

static void dialog_press(int x, int y)
{
    for (int i = 0; i < dialog.count; i++) {
        if (gfx_inside(x, y, dialog_button_x(i), dialog_y() + DIALOG_H - 32, 72, 22)) {
            dialog.hot = i;
            dialog_key('\n');
            return;
        }
    }
}

static void draw_dialog(void)
{
    int x = dialog_x(), y = dialog_y();

    gfx_unclip();
    gfx_darken(0, 0, SCREEN_W, SCREEN_H, 90); /* everything else waits */
    gfx_shadow(x, y, DIALOG_W, DIALOG_H);
    gfx_fill(x, y, DIALOG_W, DIALOG_H, theme->chrome);
    gfx_chrome(x + 2, y + 2, DIALOG_W - 4, TITLE_H, theme->chrome, 0);
    gfx_text_scaled(x + 9, y + 7, dialog.title, 2, theme->accent);
    gfx_fill(x, y + 2 + TITLE_H, DIALOG_W, SEP, theme->line);
    gfx_frame(x, y, DIALOG_W, DIALOG_H);
    gfx_fill(x + 6, y + 6 + TITLE_H, DIALOG_W - 12, DIALOG_H - 12 - TITLE_H, theme->window);
    gfx_icon(x + 14, y + 38, ICON_POWER, 2);
    gfx_text_n(x + 56, y + 42, dialog.message, (DIALOG_W - 66) / CHAR_W, theme->text);
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
        gfx_text(x + 56, y + 56, line, theme->dim);
    }
    for (int i = 0; i < dialog.count; i++)
        gfx_button(dialog_button_x(i), y + DIALOG_H - 32, 72, 22, dialog.buttons[i], i == dialog.hot);
}

/* ---- The launch menu, opened by the B ------------------------------------------ */

static int menu_height(void)
{
    return MENU_HEAD + LAUNCHER_COUNT * MENU_ROW + RUN_H + POWER_H + 20;
}

static int menu_top(void)
{
    return work_bottom() - menu_height() - 2;
}

static void launch(int index)
{
    const struct launcher *l = &launchers[index];

    menu_open = 0;
    if (l->app) {
        wm_open(l->app, 0);
    } else {
        char *argv[1];

        argv[0] = (char *)l->program;
        bdeskui_run_program(1, argv);
    }
}

static void submit_run_field(void)
{
    if (run_length == 0) {
        launch(menu_selected);
        return;
    }
    menu_open = 0;
    terminal_run(run_command);
    run_length = 0;
    run_command[0] = 0;
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
    return 4 + 6 + i * (power_button_width() + 4);
}

static int power_y(void)
{
    return menu_top() + MENU_HEAD + 4 + LAUNCHER_COUNT * MENU_ROW + RUN_H + 4;
}

static void draw_menu(int mouse_x, int mouse_y)
{
    int top = menu_top(), x = 4, y;

    gfx_unclip();
    gfx_shadow(x, top, MENU_W, menu_height());
    gfx_chrome(x, top, MENU_W, menu_height(), theme->chrome, 0);
    gfx_text_scaled(x + 10, top + 8, bdeskui_user(), 2, theme->accent);
    gfx_text(x + MENU_W - 13 * CHAR_W - 8, top + 12, "BDeskUI/BShit", theme->idle);
    gfx_fill(x, top + MENU_HEAD - 2, MENU_W, 2, theme->line);

    for (int i = 0; i < LAUNCHER_COUNT; i++) {
        int row = top + MENU_HEAD + 2 + i * MENU_ROW;
        int hot = i == menu_selected || gfx_inside(mouse_x, mouse_y, x, row, MENU_W, MENU_ROW);

        if (hot)
            gfx_gel(x + 4, row + 1, MENU_W - 8, MENU_ROW - 2, theme->select, 0);
        gfx_icon(x + 8, row + 4, launchers[i].icon, 1);
        gfx_text_bold(x + 32, row + 4, launchers[i].label, theme->chrome_text);
        gfx_text(x + 32, row + 13, launchers[i].blurb, theme->idle);
    }

    y = top + MENU_HEAD + 4 + LAUNCHER_COUNT * MENU_ROW;
    gfx_text_bold(x + 8, y + 7, "run", theme->chrome_text);
    gfx_inset(x + 38, y + 3, MENU_W - 48, RUN_H - 6, theme->field);
    gfx_text_n(x + 42, y + 7, run_command, (MENU_W - 56) / CHAR_W, theme->text);
    if (wm_frame() / 25 % 2 == 0)
        gfx_fill(x + 42 + run_length * CHAR_W, y + 6, 1, 10, theme->text);

    y = power_y();
    for (int i = 0; i < POWER_COUNT; i++) {
        int bx = power_button_x(i), pressed = gfx_inside(mouse_x, mouse_y, bx, y, power_button_width(), POWER_H - 4) &&
                                              (last_buttons & BS_MOUSE_LEFT);

        gfx_slab(bx, y, power_button_width(), POWER_H - 4, power_buttons[i].color, pressed);
        gfx_text(bx + (power_button_width() - (int)strlen(power_buttons[i].label) * CHAR_W) / 2, y + 6,
                 power_buttons[i].label, 0xFFFFFF);
    }
    gfx_frame(x, top, MENU_W, menu_height());
}

static int menu_press(int x, int y)
{
    int top = menu_top(), row_top = top + MENU_HEAD + 2;

    if (!gfx_inside(x, y, 4, top, MENU_W, menu_height()))
        return 0;
    if (y >= row_top && y < row_top + LAUNCHER_COUNT * MENU_ROW) {
        launch((y - row_top) / MENU_ROW);
        return 1;
    }
    for (int i = 0; i < POWER_COUNT; i++)
        if (gfx_inside(x, y, power_button_x(i), power_y(), power_button_width(), POWER_H - 4))
            leave(power_buttons[i].action);
    return 1;
}

/* ---- Desktop icons: tiles in columns, filled top to bottom ----------------------- */

static int icons_per_column(void)
{
    int rows = (work_bottom() - 8) / ICON_PITCH_Y;

    return rows > 0 ? rows : 1;
}

static int icon_x(int i) { return 6 + i / icons_per_column() * ICON_PITCH_X; }
static int icon_y(int i) { return 7 + i % icons_per_column() * ICON_PITCH_Y; }

static int icon_at(int x, int y)
{
    for (int i = 0; i < LAUNCHER_COUNT; i++)
        if (gfx_inside(x, y, icon_x(i), icon_y(i), TILE + 8, ICON_PITCH_Y - 4))
            return i;
    return -1;
}

static void draw_icons(void)
{
    gfx_unclip();
    for (int i = 0; i < LAUNCHER_COUNT; i++) {
        int x = icon_x(i), y = icon_y(i), label_w = (int)strlen(launchers[i].label) * CHAR_W;

        gfx_chrome(x, y, TILE, TILE, theme->tile, 0);
        if (i == icon_selected)
            gfx_frame(x - 2, y - 2, TILE + 4, TILE + 4);
        gfx_icon(x + (TILE - 32) / 2, y + (TILE - 32) / 2, launchers[i].icon, 2);
        gfx_text(x + (TILE - label_w) / 2 + 1, y + TILE + 5, launchers[i].label, INK);
        gfx_text(x + (TILE - label_w) / 2, y + TILE + 4, launchers[i].label, 0xFFFFFF);
    }
}

/* ---- The taskbar: B, then a [name | ^ | X] per window, the tray on the right ---- */

static int task_name_width(void)
{
    int room = SCREEN_W - TRAY_W - 14 - (4 + B_SIZE + 8);
    int each = window_count ? room / window_count - 6 - 2 * TASK_BUTTON : TASK_NAME_MAX;

    return each < TASK_NAME_MAX ? each : TASK_NAME_MAX;
}

static int task_x(int i)
{
    return 4 + B_SIZE + 8 + i * (task_name_width() + 2 * TASK_BUTTON + 6);
}

static void press_taskbar(int x)
{
    if (x < 4 + B_SIZE + 4) {
        menu_open = !menu_open;
        menu_selected = 0;
        return;
    }
    for (int i = 0; i < window_count; i++) {
        int left = task_x(i), name_w = task_name_width();

        if (x >= left && x < left + name_w) {
            if (focused() == &windows[i])
                windows[i].minimized = 1; /* the active window's button hides it */
            else
                raise_window(i);
            return;
        }
        if (x >= left + name_w && x < left + name_w + TASK_BUTTON) {
            raise_window(i);
            toggle_maximize(&windows[window_count - 1]);
            return;
        }
        if (x >= left + name_w + TASK_BUTTON && x < left + name_w + 2 * TASK_BUTTON) {
            request_close(i);
            return;
        }
    }
}

/* ---- Input -------------------------------------------------------------------- */

static int window_at(int x, int y)
{
    for (int i = window_count - 1; i >= 0; i--) {
        const struct window *w = &windows[i];

        if (!w->minimized && gfx_inside(x, y, outer_left(w), outer_top(w), outer_width(w), outer_height(w)))
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
    if (y >= work_bottom()) {
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

        if (gfx_inside(x, y, title_left(w), title_top(w), title_width(w), TITLE_H)) {
            if (gfx_inside(x, y, button_x(w, 0), title_top(w), TITLE_BUTTON, TITLE_H)) {
                w->minimized = 1;
            } else if (gfx_inside(x, y, button_x(w, 1), title_top(w), TITLE_BUTTON, TITLE_H)) {
                toggle_maximize(w);
            } else if (gfx_inside(x, y, button_x(w, 2), title_top(w), TITLE_BUTTON, TITLE_H)) {
                request_close(window_count - 1);
            } else if (double_click) {
                toggle_maximize(w);
            } else {
                drag = DRAG_MOVE;
                drag_dx = x - w->x;
                drag_dy = y - w->y;
            }
        } else if (gfx_inside(x, y, w->x + w->w - GRIP, w->y + w->h - GRIP, GRIP + FRAME, GRIP + FRAME)) {
            drag = DRAG_RESIZE;
            drag_dx = w->x + w->w - x;
            drag_dy = w->y + w->h - y;
        } else if (gfx_inside(x, y, w->x, w->y, w->w, w->h) && w->app->press) {
            drag = DRAG_CONTENT;
            w->app->press(x - w->x, y - w->y, w->w, w->h);
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
        w->app->drag(x - w->x, y - w->y, w->w, w->h);
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
    if (key == CTRL('q')) {
        leave(ACTION_TTY);
    } else if (key == CTRL('w')) {
        if (w)
            request_close(window_count - 1);
    } else if (key == CTRL('n')) {
        if (window_count > 1)
            raise_window(0); /* cycle: the bottom window comes to the front */
        else if (window_count == 1)
            windows[0].minimized = 0;
    } else if (w && w->app->key) {
        w->app->key(key);
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

/* The title bar glyphs: line-drawn V, ^ and X, centred in a segment. */
static void title_glyph(int x, int y, int which, unsigned int color)
{
    int cx = x + TITLE_BUTTON / 2, cy = y + TITLE_H / 2;

    if (which == 0) {
        gfx_line(cx - 6, cy - 4, cx - 1, cy + 4, 2, color);
        gfx_line(cx, cy + 4, cx + 5, cy - 4, 2, color);
    } else if (which == 1) {
        gfx_line(cx - 6, cy + 4, cx - 1, cy - 4, 2, color);
        gfx_line(cx, cy - 4, cx + 5, cy + 4, 2, color);
    } else {
        gfx_line(cx - 5, cy - 5, cx + 4, cy + 4, 2, color);
        gfx_line(cx + 4, cy - 5, cx - 5, cy + 4, 2, color);
    }
}

static void draw_window(const struct window *w, int is_focused)
{
    int top = title_top(w), left = title_left(w), width = title_width(w);
    unsigned int text = is_focused ? theme->chrome_text : theme->idle;

    gfx_unclip();
    gfx_shadow(outer_left(w), outer_top(w), outer_width(w), outer_height(w));
    gfx_fill(outer_left(w), outer_top(w), outer_width(w), outer_height(w), theme->chrome);
    gfx_chrome(left, top, width, TITLE_H, is_focused ? LIGHTER(theme->chrome, 18) : theme->chrome, 0);
    gfx_text_scaled(left + 6, top + 5, w->app->name, 2, text);
    for (int i = 0; i < 3; i++) {
        int bx = button_x(w, i);

        gfx_fill(bx, top, 2, TITLE_H, theme->line); /* the segment's left edge */
        title_glyph(bx + 1, top, i, i == 2 && is_focused ? theme->accent : text);
    }
    gfx_fill(outer_left(w), w->y - SEP, outer_width(w), SEP, theme->line);
    gfx_frame(outer_left(w), outer_top(w), outer_width(w), outer_height(w));

    gfx_clip(w->x, w->y, w->w, w->h);
    gfx_fill(0, 0, w->w, w->h, theme->window);
    w->app->draw(w->w, w->h, is_focused);

    /* The grip: three ridges in the corner. */
    for (int ridge = 0; ridge < 3; ridge++) {
        int reach = 3 + ridge * 3;

        for (int i = 0; i < reach; i++)
            gfx_fill(w->w - 2 - reach + i, w->h - 2 - i, 1, 1, theme->dim);
    }
    gfx_unclip();
}

static void draw_taskbar(void)
{
    int top = work_bottom(), name_w = task_name_width();

    gfx_unclip();
    gfx_chrome(0, top, SCREEN_W, TASKBAR_H, theme->chrome, 0);
    gfx_fill(0, top, SCREEN_W, 2, theme->line);

    /* The B: a framed slab with the letter big and bright. */
    gfx_chrome(4, top + 4, B_SIZE, B_SIZE, theme->chrome, menu_open);
    gfx_frame(4, top + 4, B_SIZE, B_SIZE);
    gfx_text_scaled(4 + (B_SIZE - 15) / 2 + 1, top + 4 + (B_SIZE - 21) / 2 + 1, "B", 3, INK);
    gfx_text_scaled(4 + (B_SIZE - 15) / 2, top + 4 + (B_SIZE - 21) / 2, "B", 3, theme->accent);

    for (int i = 0; i < window_count; i++) {
        int left = task_x(i), active = focused() == &windows[i];
        int total = name_w + 2 * TASK_BUTTON, y = top + 7, h = TASKBAR_H - 12;
        unsigned int text = windows[i].minimized ? theme->idle : theme->chrome_text;
        int chars = (name_w - 8) / (2 * CHAR_W);

        gfx_chrome(left, y, total, h, active ? LIGHTER(theme->chrome, 30) : theme->chrome, active);
        if (chars >= 4) { /* the name big when it fits, small when it does not */
            char name[16];

            copy_string(name, sizeof(name), windows[i].app->name);
            if ((int)strlen(name) > chars)
                name[chars] = 0;
            gfx_text_scaled(left + 5, y + (h - 14) / 2, name, 2, text);
        } else {
            gfx_text_n(left + 4, y + (h - 7) / 2, windows[i].app->name, (name_w - 6) / CHAR_W, text);
        }
        for (int b = 0; b < 2; b++) {
            int bx = left + name_w + b * TASK_BUTTON, cx = bx + TASK_BUTTON / 2, cy = y + h / 2;

            gfx_fill(bx, y, 2, h, theme->line);
            if (b == 0) {
                gfx_line(cx - 5, cy + 3, cx - 1, cy - 4, 2, text);
                gfx_line(cx, cy - 4, cx + 4, cy + 3, 2, text);
            } else {
                gfx_line(cx - 4, cy - 4, cx + 3, cy + 3, 2, text);
                gfx_line(cx + 3, cy - 4, cx - 4, cy + 3, 2, text);
            }
        }
        gfx_frame(left, y, total, h);
    }

    gfx_text_scaled(SCREEN_W - 5 * 2 * CHAR_W - 8, top + 13, clock_text, 2, theme->chrome_text);
    gfx_text(SCREEN_W - 5 * 2 * CHAR_W - 18 - (int)strlen(address_text) * CHAR_W, top + 18, address_text, theme->idle);
}

void wm_render(int mouse_x, int mouse_y)
{
    enum pointer_kind pointer = POINTER_ARROW;
    struct window *w = focused();

    gfx_draw_wallpaper(0, SCREEN_H);
    draw_icons();
    for (int i = 0; i < window_count; i++)
        if (!windows[i].minimized)
            draw_window(&windows[i], &windows[i] == focused());
    draw_taskbar();
    if (menu_open)
        draw_menu(mouse_x, mouse_y);
    if (dialog.open)
        draw_dialog();

    if (drag == DRAG_MOVE)
        pointer = POINTER_MOVE;
    else if (drag == DRAG_RESIZE ||
             (w && !dialog.open && gfx_inside(mouse_x, mouse_y, w->x + w->w - GRIP, w->y + w->h - GRIP, GRIP + FRAME, GRIP + FRAME)))
        pointer = POINTER_RESIZE;
    gfx_pointer(mouse_x, mouse_y, pointer);
    gfx_present();
}
