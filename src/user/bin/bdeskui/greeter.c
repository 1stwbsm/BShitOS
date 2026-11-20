/*
 * The graphical login: started by login (as root) where there is a graphics
 * mode. Logging in switches this very process to the account, and the desktop
 * session carries on in it. "TTY" hands back to login's text prompt.
 */
#include "bdeskui.h"

#define FRAME_MS 15
#define PANEL_W 300
#define PANEL_H 182
#define USERS_MAX 4
#define FIELD_MAX 32

static char users[USERS_MAX][BS_USER_NAME_MAX];
static int user_count;
static char name[FIELD_MAX], password[FIELD_MAX], message[48];
static int field; /* 0 name, 1 password */
static int hot_button = -1;

static const char *const button_labels[4] = {"log in", "TTY", "restart", "shutdown"};

static void read_users(void)
{
    char table[2048];
    int fd = open("/conf/passwd", BS_O_READ), got;

    user_count = 0;
    if (fd < 0)
        return;
    got = read(fd, table, sizeof(table) - 1);
    close(fd);
    if (got <= 0)
        return;
    table[got] = 0;
    for (char *line = table; *line && user_count < USERS_MAX;) {
        char *end = strchr(line, '\n'), *colon = strchr(line, ':');

        if (end)
            *end = 0;
        if (line[0] != '#' && colon && atoi(colon + 1) >= 1000) { /* people, not root */
            unsigned int n = (unsigned int)(colon - line);

            if (n < BS_USER_NAME_MAX) {
                memcpy(users[user_count], line, n);
                users[user_count++][n] = 0;
            }
        }
        line = end ? end + 1 : line + strlen(line);
    }
}

/* ---- Layout -------------------------------------------------------------------- */

static int panel_x(void) { return (SCREEN_W - PANEL_W) / 2; }
static int panel_y(void) { return (SCREEN_H - PANEL_H) / 2 + 12; }
static int body_y(void) { return panel_y() + panel_title_h() + 6; }
static int tile_x(int i) { return panel_x() + (PANEL_W - user_count * 64) / 2 + i * 64 + 14; }
static int fields_y(void) { return body_y() + (user_count ? 62 : 6); }
static int field_x(void) { return panel_x() + 84; }
static int field_w(void) { return PANEL_W - 100; }
static int buttons_y(void) { return fields_y() + 48; }
static int button_w(void) { return (PANEL_W - 24 - 3 * 6) / 4; }
static int button_x(int i) { return panel_x() + 12 + i * (button_w() + 6); }

static void draw(int mouse_x, int mouse_y)
{
    struct bs_time t;
    char clock[6], dots[FIELD_MAX];
    int x = panel_x(), n = (int)strlen(password);

    gfx_background();
    gfx_text_scaled((SCREEN_W - 7 * 3 * CHAR_W) / 2, panel_y() - 34, "BShitOS", 3, skin->accent);
    skin->draw_panel(x, panel_y(), PANEL_W, PANEL_H, "log in");
    for (int i = 0; i < user_count; i++)
        skin->draw_tile(tile_x(i), body_y() + 2, 36, ICON_USER, users[i], strcmp(users[i], name) == 0);

    gfx_text_bold(x + 14, fields_y() + 4, "name", skin->text);
    skin->draw_field(field_x(), fields_y(), field_w(), 16, field == 0);
    gfx_text(field_x() + 4, fields_y() + 4, name, skin->text);
    gfx_text_bold(x + 14, fields_y() + 26, "password", skin->text);
    skin->draw_field(field_x(), fields_y() + 22, field_w(), 16, field == 1);
    for (int i = 0; i < n && i < FIELD_MAX - 1; i++)
        dots[i] = '*';
    dots[n < FIELD_MAX ? n : FIELD_MAX - 1] = 0;
    gfx_text(field_x() + 4, fields_y() + 26, dots, skin->text);
    if (wm_frame() / 25 % 2 == 0) {
        int caret = (int)strlen(field ? dots : name);

        gfx_fill(field_x() + 4 + caret * CHAR_W, fields_y() + 3 + field * 22, 1, 10, skin->text);
    }
    for (int i = 0; i < 4; i++)
        skin->draw_button(button_x(i), buttons_y(), button_w(), 18, button_labels[i],
                          i == hot_button || gfx_inside(mouse_x, mouse_y, button_x(i), buttons_y(), button_w(), 18));
    if (message[0])
        gfx_text(x + (PANEL_W - (int)strlen(message) * CHAR_W) / 2, buttons_y() + 26, message, skin->accent);

    get_time(&t);
    clock[0] = (char)('0' + t.hour / 10);
    clock[1] = (char)('0' + t.hour % 10);
    clock[2] = ':';
    clock[3] = (char)('0' + t.minute / 10);
    clock[4] = (char)('0' + t.minute % 10);
    clock[5] = 0;
    gfx_text_scaled(SCREEN_W - 5 * 2 * CHAR_W - 10, SCREEN_H - 24, clock, 2, skin->accent);
    gfx_pointer(mouse_x, mouse_y, POINTER_ARROW);
    gfx_present();
}

/* ---- Doing ---------------------------------------------------------------------- */

static int try_login(void)
{
    int rc;

    if (!name[0]) {
        copy_string(message, sizeof(message), "who are you? (a name, first)");
        field = 0;
        return -1;
    }
    copy_string(message, sizeof(message), "checking...");
    draw(-100, -100);
    rc = login(name, password);
    memset(password, 0, sizeof(password));
    if (rc < 0) {
        copy_string(message, sizeof(message), "wrong name or password");
        field = 1;
        return -1;
    }
    return 0;
}

/* Returns -1 to stay, or what the greeter should return. */
static int act(int button)
{
    switch (button) {
    case 0:
        return try_login() == 0 ? 0 : -1;
    case 1:
        return EXIT_TTY_LOGIN;
    case 2:
        shutdown(BS_SHUTDOWN_REBOOT);
        return EXIT_LOGOUT;
    case 3:
        shutdown(BS_SHUTDOWN_POWEROFF);
        return EXIT_LOGOUT;
    default:
        return -1;
    }
}

static int key(int k)
{
    char *text = field ? password : name;
    unsigned int n = strlen(text);

    if (k == '\t' || k == BS_KEY_UP || k == BS_KEY_DOWN) {
        field = !field;
    } else if (k == '\n') {
        if (field == 0 && name[0])
            field = 1;
        else
            return act(0);
    } else if (k == '\b') {
        if (n)
            text[n - 1] = 0;
    } else if (k == 21) { /* Ctrl+U */
        text[0] = 0;
    } else if (k >= 0x20 && k < 0x7F && n + 1 < FIELD_MAX) {
        text[n] = (char)k;
        text[n + 1] = 0;
        message[0] = 0;
    }
    return -1;
}

static int press(int x, int y)
{
    for (int i = 0; i < user_count; i++) {
        if (gfx_inside(x, y, tile_x(i) - 6, body_y(), 48, 58)) {
            copy_string(name, sizeof(name), users[i]);
            password[0] = 0;
            field = 1;
            return -1;
        }
    }
    if (gfx_inside(x, y, field_x(), fields_y(), field_w(), 16))
        field = 0;
    else if (gfx_inside(x, y, field_x(), fields_y() + 22, field_w(), 16))
        field = 1;
    for (int i = 0; i < 4; i++)
        if (gfx_inside(x, y, button_x(i), buttons_y(), button_w(), 18))
            return act(i);
    return -1;
}

int greeter_run(const char *auto_user)
{
    int x = SCREEN_W / 2, y = SCREEN_H * 3 / 4;
    unsigned int buttons = 0;
    struct bs_mouse mouse;

    if (auto_user && login(auto_user, 0) == 0) /* the live system: straight in */
        return 0;
    read_users();
    name[0] = password[0] = message[0] = 0;
    if (user_count == 1) { /* one person: their name is already there */
        copy_string(name, sizeof(name), users[0]);
        field = 1;
    }
    mouse_read(&mouse);
    for (int dirty = 1;; dirty = 0) {
        int k, result = -1;

        if (shutting_down())
            return EXIT_LOGOUT;
        mouse_read(&mouse);
        if (mouse.dx || mouse.dy || mouse.buttons != buttons) {
            dirty = 1;
            int pressed = (mouse.buttons & BS_MOUSE_LEFT) && !(buttons & BS_MOUSE_LEFT);

            x += mouse.dx * settings_mouse_speed();
            y += mouse.dy * settings_mouse_speed();
            x = x < 0 ? 0 : x >= SCREEN_W ? SCREEN_W - 1 : x;
            y = y < 0 ? 0 : y >= SCREEN_H ? SCREEN_H - 1 : y;
            buttons = mouse.buttons;
            if (pressed)
                result = press(x, y);
        }
        while (result < 0 && (k = poll_key()) != 0) {
            result = key(k);
            dirty = 1;
        }
        if (result >= 0)
            return result;
        if (wm_tick() || dirty) /* input, or the caret's blink */
            draw(x, y);
        sleep_ms(FRAME_MS);
    }
}
