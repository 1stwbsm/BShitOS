/* Settings: per user in ~/.desk.conf (defaults in /conf/desk.conf), saved as soon as they change. */
#include "bs.h"
#include "desk.h"

#define SYSTEM_CONF "/conf/desk.conf"
#define USER_CONF "/.desk.conf"
#define LEFT 10
#define CARD_H 34
#define CARD_GAP 6

static const struct theme themes[] = {
    /* name     desk      window    text      dim       line      rail      rail_text select    field */
    {"graphite", 0x22262C, 0x2C3138, 0xE6E8EB, 0x8C939D, 0x4A515B, 0x181B1F, 0xD0D4DA, 0x3D4A5C, 0x1E2227},
    {"daylight", 0xB9C2CC, 0xF4F5F7, 0x1D2126, 0x6A727D, 0xA8B0BA, 0x2A2F36, 0xE6E8EB, 0xCFE0F5, 0xFFFFFF},
    {"midnight", 0x07090C, 0x10141A, 0xD6DCE4, 0x6C7684, 0x2A313B, 0x000000, 0xC4CAD2, 0x1C2B40, 0x0A0D11},
};
#define THEME_COUNT ((int)(sizeof(themes) / sizeof(themes[0])))

const struct theme *theme = &themes[0];
static int mouse_speed = 1;
static char status[48];

const char *desk_home(void)
{
    static struct bs_user me;

    if (!me.home[0])
        whoami(&me);
    return me.home;
}

static void user_conf(char *out, unsigned int size)
{
    out[0] = 0;
    if (strlen(desk_home()) + sizeof(USER_CONF) < size) {
        copy_string(out, size, desk_home());
        memcpy(out + strlen(out), USER_CONF, sizeof(USER_CONF));
    }
}

int desk_conf_get(const char *key, char *out, unsigned int size)
{
    char path[BS_PATH_MAX];

    user_conf(path, sizeof(path));
    if (path[0] && conf_get(path, key, out, size) == 0)
        return 0;
    return conf_get(SYSTEM_CONF, key, out, size);
}

int desk_conf_set(const char *key, const char *value)
{
    char path[BS_PATH_MAX];

    user_conf(path, sizeof(path));
    return path[0] ? conf_set(path, key, value) : -BS_ENAMETOOLONG;
}

int settings_mouse_speed(void)
{
    return mouse_speed;
}

void settings_load(void)
{
    char value[16];

    if (desk_conf_get("theme", value, sizeof(value)) == 0)
        for (int i = 0; i < THEME_COUNT; i++)
            if (strcmp(value, themes[i].name) == 0)
                theme = &themes[i];
    if (desk_conf_get("mouse_speed", value, sizeof(value)) == 0) {
        int speed = atoi(value);

        if (speed >= 1 && speed <= 3)
            mouse_speed = speed;
    }
}

static void saved(int rc)
{
    copy_string(status, sizeof(status), rc < 0 ? strerror(rc) : "saved to ~/.desk.conf");
}

static void settings_draw(int w, int h, int focused)
{
    int y = 8;

    (void)h;
    (void)focused;
    gfx_text(LEFT, y, "Theme", theme->dim);
    y += 12;
    for (int i = 0; i < THEME_COUNT; i++) {
        const struct theme *t = &themes[i];

        gfx_fill(LEFT, y, w - 2 * LEFT, CARD_H, t->desk);
        gfx_fill(LEFT + 8, y + 6, 70, CARD_H - 12, t->window);
        gfx_fill(LEFT + 8, y + 6, 30, 5, app_terminal.accent);
        gfx_text(LEFT + 12, y + 15, "Abc", t->text);
        gfx_text(LEFT + 90, y + 13, t->name, t->text);
        gfx_outline(LEFT, y, w - 2 * LEFT, CARD_H, t == theme ? app_settings.accent : t->line);
        if (t == theme) {
            gfx_outline(LEFT + 1, y + 1, w - 2 * LEFT - 2, CARD_H - 2, app_settings.accent);
            gfx_text(w - LEFT - 8 * CHAR_W, y + 13, "in use", app_settings.accent);
        }
        y += CARD_H + CARD_GAP;
    }

    y += 4;
    gfx_text(LEFT, y + 4, "Mouse speed", theme->dim);
    for (int s = 1; s <= 3; s++) {
        char label[3] = {(char)('0' + s), 'x', 0};

        gfx_button(LEFT + 80 + (s - 1) * 36, y, 32, 16, label, s == mouse_speed);
    }
    y += 28;
    gfx_text(LEFT, y, status, theme->dim);
}

static void settings_press(int x, int y, int w, int h)
{
    int top = 20;

    (void)h;
    for (int i = 0; i < THEME_COUNT; i++) {
        if (gfx_inside(x, y, LEFT, top + i * (CARD_H + CARD_GAP), w - 2 * LEFT, CARD_H)) {
            theme = &themes[i];
            saved(desk_conf_set("theme", themes[i].name));
            return;
        }
    }
    top += THEME_COUNT * (CARD_H + CARD_GAP) + 4;
    for (int s = 1; s <= 3; s++) {
        if (gfx_inside(x, y, LEFT + 80 + (s - 1) * 36, top, 32, 16)) {
            char value[2] = {(char)('0' + s), 0};

            mouse_speed = s;
            saved(desk_conf_set("mouse_speed", value));
            return;
        }
    }
}

const struct app app_settings = {
    .name = "Settings", .blurb = "theme and mouse", .accent = 0x56C2D6,
    .default_w = 300, .default_h = 210, .min_w = 240, .min_h = 190,
    .draw = settings_draw, .press = settings_press,
};
