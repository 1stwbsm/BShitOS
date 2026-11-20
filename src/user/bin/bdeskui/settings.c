/* Settings: the skin and the mouse speed, per user in ~/.bdeskui.conf (defaults in /conf/bdeskui.conf). */
#include "bdeskui.h"

#define SYSTEM_CONF "/conf/bdeskui.conf"
#define USER_CONF "/.bdeskui.conf"
#define LEFT 10
#define CARD_H 34
#define CARD_GAP 6

static const struct theme themes[] = {
    {.name = "aqua", .sky_top = 0x0E5AA8, .sky_bottom = 0x86DAF2, .glass_top = 0x2C6680, .glass_bottom = 0x0C2836,
     .accent = 0x5CC83A, .idle = 0x7E95A3,
     .window = 0xEEF4F7, .text = 0x0E1C24, .dim = 0x587080, .line = 0x8DA4B0, .select = 0xBDE6F7, .field = 0xFFFFFF},
    {.name = "meadow", .sky_top = 0x2A8FD8, .sky_bottom = 0xCDEFA0, .glass_top = 0x2F6E40, .glass_bottom = 0x0E2C16,
     .accent = 0x48A8E8, .idle = 0x8AA68C,
     .window = 0xF1F6EC, .text = 0x14220E, .dim = 0x5C7254, .line = 0x98AE8E, .select = 0xD2EDB8, .field = 0xFFFFFF},
    {.name = "dusk", .sky_top = 0x231A52, .sky_bottom = 0xF2905C, .glass_top = 0x4C3062, .glass_bottom = 0x180C24,
     .accent = 0xF0A030, .idle = 0x8A7C9C,
     .window = 0xF3EFF5, .text = 0x1C1426, .dim = 0x6A5A78, .line = 0xA898B4, .select = 0xF6D8C8, .field = 0xFFFFFF},
    {.name = "concrete", .sky_top = 0x666C70, .sky_bottom = 0xCDD0D1, .glass_top = 0x3C4044, .glass_bottom = 0x141618,
     .accent = 0xF0602A, .idle = 0x8A9094,
     .window = 0xE9E9E6, .text = 0x111111, .dim = 0x5E6264, .line = 0x9A9EA0, .select = 0xF5D0B8, .field = 0xFFFFFF},
};
#define THEME_COUNT ((int)(sizeof(themes) / sizeof(themes[0])))

const struct theme *theme = &themes[0];
static int mouse_speed = 1, autostart = 1;
static char status[48];
static struct bs_user me;

static const struct bs_user *user(void)
{
    if (!me.name[0])
        whoami(&me);
    return &me;
}

const char *bdeskui_home(void)
{
    return user()->home;
}

const char *bdeskui_user(void)
{
    return user()->name;
}

static void user_conf(char *out, unsigned int size)
{
    out[0] = 0;
    if (strlen(bdeskui_home()) + sizeof(USER_CONF) < size) {
        copy_string(out, size, bdeskui_home());
        memcpy(out + strlen(out), USER_CONF, sizeof(USER_CONF));
    }
}

int bdeskui_conf_get(const char *key, char *out, unsigned int size)
{
    char path[BS_PATH_MAX];

    user_conf(path, sizeof(path));
    if (path[0] && conf_get(path, key, out, size) == 0)
        return 0;
    return conf_get(SYSTEM_CONF, key, out, size);
}

int bdeskui_conf_set(const char *key, const char *value)
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

    if (bdeskui_conf_get("skin", value, sizeof(value)) == 0)
        for (int i = 0; i < THEME_COUNT; i++)
            if (strcmp(value, themes[i].name) == 0)
                theme = &themes[i];
    if (bdeskui_conf_get("mouse_speed", value, sizeof(value)) == 0) {
        int speed = atoi(value);

        if (speed >= 1 && speed <= 3)
            mouse_speed = speed;
    }
    if (bdeskui_conf_get("autostart", value, sizeof(value)) == 0)
        autostart = strcmp(value, "no") != 0;
}

static void saved(int rc)
{
    copy_string(status, sizeof(status), rc < 0 ? strerror(rc) : "saved to ~/.bdeskui.conf");
}

static void settings_draw(int w, int h, int focused)
{
    int y = 8;

    (void)h;
    (void)focused;
    gfx_text_bold(LEFT, y, "Skin", theme->text);
    y += 13;
    for (int i = 0; i < THEME_COUNT; i++) {
        const struct theme *t = &themes[i];
        int card_w = w - 2 * LEFT;

        /* a small window on that skin's sky */
        gfx_gradient(LEFT, y, card_w, CARD_H, t->sky_top, t->sky_bottom);
        gfx_fill(LEFT + 10, y + 6, 74, CARD_H - 12, INK);
        gfx_gel(LEFT + 11, y + 7, 72, 8, t->accent, 0);
        gfx_fill(LEFT + 11, y + 15, 72, CARD_H - 22, t->window);
        gfx_text(LEFT + 15, y + 17, "Abc", t->text);
        gfx_text_raised(LEFT + 96, y + 13, t->name, 0xFFFFFF);
        gfx_outline(LEFT, y, card_w, CARD_H, INK);
        if (t == theme) {
            gfx_frame(LEFT - 1, y - 1, card_w + 2, CARD_H + 2);
            gfx_slab(LEFT + card_w - 54, y + 9, 46, 16, 0x5CC83A, 0);
            gfx_text(LEFT + card_w - 49, y + 13, "in use", INK);
        }
        y += CARD_H + CARD_GAP;
    }

    y += 4;
    gfx_text_bold(LEFT, y + 5, "Mouse", theme->text);
    for (int s = 1; s <= 3; s++) {
        char label[3] = {(char)('0' + s), 'x', 0};

        gfx_button(LEFT + 60 + (s - 1) * 40, y, 34, 18, label, s == mouse_speed);
    }
    y += 24;
    gfx_text_bold(LEFT, y + 5, "Login", theme->text);
    gfx_button(LEFT + 60, y, 74, 18, "BDeskUI", autostart);
    gfx_button(LEFT + 140, y, 74, 18, "TTY", !autostart);
    y += 26;
    gfx_text(LEFT, y, status, theme->dim);
}

static void settings_press(int x, int y, int w, int h)
{
    int top = 21;

    (void)h;
    for (int i = 0; i < THEME_COUNT; i++) {
        if (gfx_inside(x, y, LEFT, top + i * (CARD_H + CARD_GAP), w - 2 * LEFT, CARD_H)) {
            theme = &themes[i];
            gfx_wallpaper();
            saved(bdeskui_conf_set("skin", themes[i].name));
            return;
        }
    }
    top += THEME_COUNT * (CARD_H + CARD_GAP) + 4;
    for (int s = 1; s <= 3; s++) {
        if (gfx_inside(x, y, LEFT + 60 + (s - 1) * 40, top, 34, 18)) {
            char value[2] = {(char)('0' + s), 0};

            mouse_speed = s;
            saved(bdeskui_conf_set("mouse_speed", value));
            return;
        }
    }
    top += 24;
    if (gfx_inside(x, y, LEFT + 60, top, 74, 18) || gfx_inside(x, y, LEFT + 140, top, 74, 18)) {
        autostart = x < LEFT + 140;
        saved(bdeskui_conf_set("autostart", autostart ? "yes" : "no"));
    }
}

const struct app app_settings = {
    .name = "Settings", .blurb = "skin and mouse", .accent = 0x3FB6CF, .icon = ICON_SETTINGS,
    .default_w = 300, .default_h = 280, .min_w = 240, .min_h = 264,
    .draw = settings_draw, .press = settings_press,
};
