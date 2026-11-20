/* Settings: the skin and the mouse speed, per user in ~/.bdeskui.conf (defaults in /conf/bdeskui.conf). */
#include "bdeskui.h"

#define SYSTEM_CONF "/conf/bdeskui.conf"
#define USER_CONF "/.bdeskui.conf"
#define LEFT 10
#define CARD_H 28
#define CARD_GAP 5

static const struct theme themes[] = {
    /* The default, after the BDeskUI mockup: charcoal chrome and white outlines on a navy night. */
    {.name = "night", .gloss = 44, .sky_top = 0x1B1B33, .sky_bottom = 0x0C0C14, .bubbles = 46, .line = 0xE8E8E8,
     .chrome = 0x171717, .chrome_text = 0xF4F4F4, .accent = 0xFFD95E, .idle = 0x8C8C8C, .tile = 0x0B0B0B,
     .window = 0x000000, .text = 0xE4E4E4, .dim = 0x8C8C8C, .rule = 0x2A2A2A, .select = 0x284468,
     .field = 0x0D0D0D, .button = 0x2A2A2A},
    {.name = "aqua", .gloss = 170, .sky_top = 0x0E5AA8, .sky_bottom = 0x86DAF2, .bubbles = 256, .line = 0x0B1014,
     .chrome = 0x2C6680, .chrome_text = 0xFFFFFF, .accent = 0x9CF060, .idle = 0xA9C2CE, .tile = 0x1A4A66,
     .window = 0xEEF4F7, .text = 0x0E1C24, .dim = 0x587080, .rule = 0xC8D6DE, .select = 0x9CD8F4,
     .field = 0xFFFFFF, .button = 0xC9D6DE},
    {.name = "meadow", .gloss = 150, .sky_top = 0x2A8FD8, .sky_bottom = 0xCDEFA0, .bubbles = 200, .line = 0x0F1A0C,
     .chrome = 0x2F6E40, .chrome_text = 0xFFFFFF, .accent = 0xFFE070, .idle = 0xA8C4A8, .tile = 0x1F4A2A,
     .window = 0xF1F6EC, .text = 0x14220E, .dim = 0x5C7254, .rule = 0xD2DECA, .select = 0xB8E09A,
     .field = 0xFFFFFF, .button = 0xD0DCC8},
    {.name = "dusk", .gloss = 130, .sky_top = 0x231A52, .sky_bottom = 0xF2905C, .bubbles = 160, .line = 0x140A1E,
     .chrome = 0x4C3062, .chrome_text = 0xFFFFFF, .accent = 0xFFB040, .idle = 0xB4A4C4, .tile = 0x2E1A40,
     .window = 0xF3EFF5, .text = 0x1C1426, .dim = 0x6A5A78, .rule = 0xDCD2E2, .select = 0xF0C8B0,
     .field = 0xFFFFFF, .button = 0xDCD2E2},
    {.name = "concrete", .gloss = 60, .sky_top = 0x666C70, .sky_bottom = 0xCDD0D1, .bubbles = 0, .line = 0x111111,
     .chrome = 0x9A9EA0, .chrome_text = 0x111111, .accent = 0xF0602A, .idle = 0x5E6264, .tile = 0x7A7E80,
     .window = 0xE9E9E6, .text = 0x111111, .dim = 0x5E6264, .rule = 0xD0D0CC, .select = 0xF5C8A8,
     .field = 0xFFFFFF, .button = 0xC8CAC8},
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
        gfx_fill(LEFT + 10, y + 3, 76, CARD_H - 6, t->line);
        gfx_gel(LEFT + 11, y + 4, 74, 9, t->chrome, 0);
        gfx_text(LEFT + 14, y + 5, "BS", t->accent);
        gfx_fill(LEFT + 11, y + 14, 74, CARD_H - 18, t->window);
        gfx_text(LEFT + 15, y + 15, "Abc", t->text);
        gfx_text_raised(LEFT + 98, y + 10, t->name, 0xFFFFFF);
        gfx_outline(LEFT, y, card_w, CARD_H, theme->line);
        if (t == theme) {
            gfx_frame(LEFT - 1, y - 1, card_w + 2, CARD_H + 2);
            gfx_slab(LEFT + card_w - 54, y + 6, 46, 16, 0x4C9A30, 0);
            gfx_text(LEFT + card_w - 49, y + 10, "in use", 0xFFFFFF);
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
    .name = "BSconf", .blurb = "skin, mouse, login", .accent = 0x3FB6CF, .icon = ICON_SETTINGS,
    .default_w = 300, .default_h = 274, .min_w = 240, .min_h = 250,
    .draw = settings_draw, .press = settings_press,
};
