/*
 * The window manager. Windows overlap and can be moved, resized, minimised,
 * maximised and closed. A window's title sits on a coloured tab at its top
 * left; the tab colour identifies the app.
 */
#include "bs.h"
#include "desk.h"

#define MAX_WINDOWS 8
#define TAB_H 14
#define TAB_BUTTON 12
#define GRIP 9
#define CASCADE_STEP 18
#define MENU_W 236
#define MENU_ROW 18
#define RUN_FIELD_H 20
#define RAIL_MENU_W 34
#define RAIL_TASK_W 92
#define RAIL_CLOCK_W 52
#define RAIL_NET_W 90
#define DOUBLE_CLICK_FRAMES 20
#define COMMAND_MAX 128
/* The rail is dark in every theme, so its colours are fixed. */
#define RAIL_BUTTON 0x2A3038u
#define RAIL_ACTIVE 0x43506Au
#define RAIL_DIM 0x8C939Du

struct window {
    const struct app *app;
    int x, y, w, h; /* the content area */
    int minimized, maximized;
    int saved_x, saved_y, saved_w, saved_h;
};

enum drag_kind { DRAG_NONE, DRAG_MOVE, DRAG_RESIZE, DRAG_CONTENT };
enum menu_kind { ENTRY_APP, ENTRY_PROGRAM, ENTRY_CONSOLE, ENTRY_LEAVE };

struct menu_entry {
    const char *label, *blurb;
    enum menu_kind kind;
    const struct app *app;
    const char *program;
};

static const struct menu_entry menu_entries[] = {
    {"Terminal", "run BShitOS programs", ENTRY_APP, &app_terminal, 0},
    {"Files", "browse and open files", ENTRY_APP, &app_files, 0},
    {"Notes", "write and edit text", ENTRY_APP, &app_notes, 0},
    {"Web", "browse the web (full screen)", ENTRY_PROGRAM, 0, "web"},
    {"System", "memory, disk and network", ENTRY_APP, &app_system, 0},
    {"Settings", "theme and mouse speed", ENTRY_APP, &app_settings, 0},
    {"Cube", "the spinning cube (full screen)", ENTRY_PROGRAM, 0, "cube"},
    {"Leave desk", "back to the bsh prompt", ENTRY_LEAVE, 0, 0},
};
#define MENU_COUNT ((int)(sizeof(menu_entries) / sizeof(menu_entries[0])))

/* Bottom of the array is drawn first; the last window has the focus. */
static struct window windows[MAX_WINDOWS];
static int window_count, cascade;
static enum drag_kind drag;
static int drag_dx, drag_dy;
static unsigned int last_buttons;
static int frame_counter, exit_requested;
static int menu_open, menu_selected;
static char run_command[COMMAND_MAX];
static int run_length;
static char clock_text[6] = "--:--", address_text[20] = "offline";
static int last_minute = -1;
static int last_click_frame = -100, last_click_x, last_click_y;

int wm_frame(void)
{
    return frame_counter;
}

void wm_request_exit(void)
{
    exit_requested = 1;
}

int wm_wants_exit(void)
{
    return exit_requested;
}

static int work_bottom(void)
{
    return SCREEN_H - RAIL_H;
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

static void clamp(struct window *w)
{
    if (w->w > SCREEN_W - 2)
        w->w = SCREEN_W - 2;
    if (w->h > work_bottom() - TAB_H - 2)
        w->h = work_bottom() - TAB_H - 2;
    if (w->x < 1)
        w->x = 1;
    if (w->y < TAB_H + 1)
        w->y = TAB_H + 1;
    if (w->x + w->w > SCREEN_W - 1)
        w->x = SCREEN_W - 1 - w->w;
    if (w->y > work_bottom() - 20)
        w->y = work_bottom() - 20;
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
        w->x = 24 + cascade * CASCADE_STEP;
        w->y = TAB_H + 12 + cascade * CASCADE_STEP;
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
        w->x = 1;
        w->y = TAB_H + 1;
        w->w = SCREEN_W - 2;
        w->h = work_bottom() - TAB_H - 2;
        w->maximized = 1;
    }
}

/* ---- Geometry of a window's tab --------------------------------------------- */

static int tab_width(const struct window *w)
{
    int width = (int)strlen(w->app->name) * CHAR_W + 16 + 3 * (TAB_BUTTON + 2);

    return width > w->w + 2 ? w->w + 2 : width;
}

static int tab_x(const struct window *w)
{
    return w->x - 1;
}

static int tab_y(const struct window *w)
{
    return w->y - 1 - TAB_H;
}

/* 0 minimise, 1 maximise, 2 close */
static int tab_button_x(const struct window *w, int which)
{
    return tab_x(w) + tab_width(w) - (3 - which) * (TAB_BUTTON + 2) - 2;
}

/* ---- The menu ---------------------------------------------------------------- */

static int menu_height(void)
{
    return MENU_COUNT * MENU_ROW + RUN_FIELD_H + 24;
}

static int menu_top(void)
{
    return work_bottom() - menu_height();
}

static void run_menu_entry(int index)
{
    const struct menu_entry *e = &menu_entries[index];

    menu_open = 0;
    if (e->kind == ENTRY_APP) {
        wm_open(e->app, 0);
    } else if (e->kind == ENTRY_PROGRAM) {
        char *argv[1];

        argv[0] = (char *)e->program;
        desk_run_fullscreen(1, argv);
    } else if (e->kind == ENTRY_LEAVE) {
        exit_requested = 1;
    }
}

static void submit_run_field(void)
{
    if (run_length == 0) {
        run_menu_entry(menu_selected);
        return;
    }
    menu_open = 0;
    terminal_run(run_command);
    run_length = 0;
    run_command[0] = 0;
}

static void menu_key(int key)
{
    if (key == KEY_ESCAPE || key == CTRL('b')) {
        menu_open = 0;
    } else if (key == BS_KEY_UP) {
        menu_selected = (menu_selected + MENU_COUNT - 1) % MENU_COUNT;
    } else if (key == BS_KEY_DOWN) {
        menu_selected = (menu_selected + 1) % MENU_COUNT;
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

static void draw_menu(int mouse_x, int mouse_y)
{
    int top = menu_top();

    gfx_unclip();
    gfx_fill(0, top, MENU_W, menu_height(), theme->window);
    gfx_outline(0, top, MENU_W, menu_height(), theme->line);
    gfx_fill(0, top, MENU_W, 3, 0xE8A33A);
    gfx_text(8, top + 8, "BShitOS", 0xE8A33A);
    gfx_text(8 + 8 * CHAR_W, top + 8, "Ctrl+B opens this menu", theme->dim);

    for (int i = 0; i < MENU_COUNT; i++) {
        int y = top + 20 + i * MENU_ROW;
        int hot = i == menu_selected || gfx_inside(mouse_x, mouse_y, 0, y, MENU_W, MENU_ROW);
        unsigned int accent = menu_entries[i].app ? menu_entries[i].app->accent : theme->dim;

        if (hot)
            gfx_fill(1, y, MENU_W - 2, MENU_ROW, theme->select);
        gfx_fill(6, y + 4, 4, MENU_ROW - 8, accent);
        gfx_text(16, y + 5, menu_entries[i].label, theme->text);
        gfx_text(16 + 11 * CHAR_W, y + 5, menu_entries[i].blurb, theme->dim);
    }

    {
        int y = top + 20 + MENU_COUNT * MENU_ROW + 2;

        gfx_text(8, y + 6, "run:", theme->dim);
        gfx_fill(36, y, MENU_W - 44, RUN_FIELD_H - 4, theme->field);
        gfx_outline(36, y, MENU_W - 44, RUN_FIELD_H - 4, theme->line);
        gfx_text_n(40, y + 4, run_command, (MENU_W - 52) / CHAR_W, theme->text);
        if (wm_frame() / 25 % 2 == 0)
            gfx_fill(40 + run_length * CHAR_W, y + 3, 1, 10, theme->text);
    }
}

/* ---- Input -------------------------------------------------------------------- */

static int window_at(int x, int y)
{
    for (int i = window_count - 1; i >= 0; i--) {
        const struct window *w = &windows[i];

        if (w->minimized)
            continue;
        if (gfx_inside(x, y, w->x - 1, w->y - 1, w->w + 2, w->h + 2) ||
            gfx_inside(x, y, tab_x(w), tab_y(w), tab_width(w), TAB_H))
            return i;
    }
    return -1;
}

static void press_rail(int x)
{
    if (x < RAIL_MENU_W) {
        menu_open = !menu_open;
        return;
    }
    for (int i = 0; i < window_count; i++) {
        int left = RAIL_MENU_W + 4 + i * (RAIL_TASK_W + 2);

        if (x >= left && x < left + RAIL_TASK_W) {
            if (focused() == &windows[i])
                windows[i].minimized = 1; /* clicking the active window hides it */
            else
                raise_window(i);
            return;
        }
    }
}

static void press(int x, int y)
{
    int index;
    int double_click = wm_frame() - last_click_frame < DOUBLE_CLICK_FRAMES &&
                       x - last_click_x < 4 && last_click_x - x < 4 && y - last_click_y < 4 && last_click_y - y < 4;

    last_click_frame = wm_frame();
    last_click_x = x;
    last_click_y = y;

    if (y >= work_bottom()) {
        press_rail(x);
        return;
    }
    if (menu_open) {
        int top = menu_top();

        if (gfx_inside(x, y, 0, top, MENU_W, menu_height())) {
            int row = (y - top - 20) / MENU_ROW;

            if (y >= top + 20 && row < MENU_COUNT)
                run_menu_entry(row);
            return;
        }
        menu_open = 0;
    }

    index = window_at(x, y);
    if (index < 0)
        return;
    raise_window(index);
    {
        struct window *w = &windows[window_count - 1];

        if (gfx_inside(x, y, tab_x(w), tab_y(w), tab_width(w), TAB_H)) {
            if (gfx_inside(x, y, tab_button_x(w, 0), tab_y(w) + 1, TAB_BUTTON, TAB_BUTTON)) {
                w->minimized = 1;
            } else if (gfx_inside(x, y, tab_button_x(w, 1), tab_y(w) + 1, TAB_BUTTON, TAB_BUTTON)) {
                toggle_maximize(w);
            } else if (gfx_inside(x, y, tab_button_x(w, 2), tab_y(w) + 1, TAB_BUTTON, TAB_BUTTON)) {
                close_window(window_count - 1);
            } else if (double_click) {
                toggle_maximize(w);
            } else {
                drag = DRAG_MOVE;
                drag_dx = x - w->x;
                drag_dy = y - w->y;
            }
        } else if (gfx_inside(x, y, w->x + w->w - GRIP, w->y + w->h - GRIP, GRIP + 1, GRIP + 1)) {
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
    if (!w)
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

    if (key == CTRL('b')) {
        menu_open = !menu_open;
        return;
    }
    if (menu_open) {
        menu_key(key);
        return;
    }
    if (key == CTRL('q')) {
        exit_requested = 1;
    } else if (key == CTRL('w')) {
        if (w)
            close_window(window_count - 1);
    } else if (key == CTRL('n')) {
        if (window_count > 1)
            raise_window(0); /* cycle: the bottom window comes to the front */
        else if (window_count == 1)
            windows[0].minimized = 0;
    } else if (w && w->app->key) {
        w->app->key(key);
    }
}

static void update_rail_info(void)
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
    update_rail_info();
    for (int i = 0; i < window_count; i++)
        if (!windows[i].minimized && windows[i].app->tick && windows[i].app->tick())
            redraw = 1;
    if (frame_counter % 25 == 0)
        redraw = 1; /* caret blink */
    return redraw;
}

/* ---- Drawing -------------------------------------------------------------------- */

static void draw_window(const struct window *w, int is_focused)
{
    unsigned int accent = w->app->accent;
    unsigned int edge = is_focused ? accent : theme->line;
    int tx = tab_x(w), ty = tab_y(w), tw = tab_width(w);
    static const char *const labels[3] = {"_", "+", "x"};

    gfx_unclip();
    gfx_fill(tx, ty, tw, TAB_H + 1, is_focused ? accent : theme->rail);
    gfx_text(tx + 6, ty + 4, w->app->name, is_focused ? 0x101010 : theme->rail_text);
    for (int i = 0; i < 3; i++) {
        int bx = tab_button_x(w, i);

        gfx_fill(bx, ty + 1, TAB_BUTTON, TAB_BUTTON, is_focused ? 0x101010 : theme->field);
        gfx_text(bx + 3, ty + 3, labels[i], is_focused ? accent : theme->dim);
    }

    gfx_outline(w->x - 1, w->y - 1, w->w + 2, w->h + 2, edge);
    gfx_clip(w->x, w->y, w->w, w->h);
    gfx_fill(0, 0, w->w, w->h, theme->window);
    w->app->draw(w->w, w->h, is_focused);

    /* The resize grip: two short diagonals in the corner. */
    for (int i = 0; i < GRIP - 2; i++) {
        gfx_fill(w->w - 2 - i, w->h - GRIP + 1 + i, 1, 1, edge);
        if (i >= 3)
            gfx_fill(w->w - 2 - i + 3, w->h - GRIP + 1 + i, 1, 1, edge);
    }
    gfx_unclip();
}

static void draw_rail(void)
{
    int top = work_bottom();

    gfx_unclip();
    gfx_fill(0, top, SCREEN_W, RAIL_H, theme->rail);
    gfx_fill(0, top, SCREEN_W, 1, theme->line);
    gfx_fill(0, top, RAIL_MENU_W, RAIL_H, menu_open ? 0xE8A33A : RAIL_BUTTON);
    gfx_text(10, top + 6, "BS", menu_open ? 0x101010 : 0xE8A33A);

    for (int i = 0; i < window_count; i++) {
        int left = RAIL_MENU_W + 4 + i * (RAIL_TASK_W + 2);
        int active = focused() == &windows[i];

        if (left + RAIL_TASK_W > SCREEN_W - RAIL_CLOCK_W - RAIL_NET_W)
            break;
        gfx_fill(left, top + 3, RAIL_TASK_W, RAIL_H - 5, active ? RAIL_ACTIVE : RAIL_BUTTON);
        gfx_fill(left, top + RAIL_H - 3, RAIL_TASK_W, 2, windows[i].minimized ? theme->line : windows[i].app->accent);
        gfx_text_n(left + 5, top + 6, windows[i].app->name, (RAIL_TASK_W - 10) / CHAR_W,
                   windows[i].minimized ? RAIL_DIM : theme->rail_text);
    }

    gfx_text(SCREEN_W - RAIL_CLOCK_W - RAIL_NET_W + 4, top + 6, address_text, RAIL_DIM);
    gfx_text(SCREEN_W - RAIL_CLOCK_W + 10, top + 6, clock_text, theme->rail_text);
}

static void draw_desktop(void)
{
    gfx_unclip();
    gfx_fill(0, 0, SCREEN_W, work_bottom(), theme->desk);
    if (window_count == 0 && !menu_open) {
        const char *hint = "Open the BS menu (bottom left, or Ctrl+B) to start something.";

        gfx_text((SCREEN_W - (int)strlen(hint) * CHAR_W) / 2, work_bottom() / 2, hint, theme->dim);
    }
}

void wm_render(int mouse_x, int mouse_y)
{
    enum pointer_kind pointer = POINTER_ARROW;
    struct window *w = focused();

    draw_desktop();
    for (int i = 0; i < window_count; i++)
        if (!windows[i].minimized)
            draw_window(&windows[i], &windows[i] == focused());
    draw_rail();
    if (menu_open)
        draw_menu(mouse_x, mouse_y);

    if (drag == DRAG_MOVE)
        pointer = POINTER_MOVE;
    else if (drag == DRAG_RESIZE || (w && gfx_inside(mouse_x, mouse_y, w->x + w->w - GRIP, w->y + w->h - GRIP, GRIP + 1, GRIP + 1)))
        pointer = POINTER_RESIZE;
    gfx_pointer(mouse_x, mouse_y, pointer);
    gfx_present();
}
