/* BSconf: skins, mouse speed, login. Per user in ~/.bdeskui.conf (defaults in /conf/bdeskui.conf). */
#include "bdeskui.h"

#define SYSTEM_CONF "/conf/bdeskui.conf"
#define USER_CONF "/.bdeskui.conf"
#define LEFT 10
#define CARD_H 48
#define CARD_GAP 5

static int mouse_speed = 1, autostart = 1, text_login_row; /* the row: only without the login screen */
static char status[48];
static struct bs_user me;

static const struct bs_user *user(void)
{
    if (!me.name[0])
        whoami(&me);
    return &me;
}

void bdeskui_forget_user(void)
{
    me.name[0] = 0;
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
    if (user()->uid == BS_UID_ROOT && strcmp(user()->home, "/root") != 0)
        return;
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
    const struct skin *chosen;

    if (bdeskui_conf_get("skin", value, sizeof(value)) == 0 && (chosen = skin_named(value)) != 0)
        skin = chosen;
    if (bdeskui_conf_get("mouse_speed", value, sizeof(value)) == 0) {
        int speed = atoi(value);

        if (speed >= 1 && speed <= 3)
            mouse_speed = speed;
    }
    if (bdeskui_conf_get("autostart", value, sizeof(value)) == 0)
        autostart = strcmp(value, "no") != 0;
    text_login_row = conf_get("/conf/login.conf", "graphical", value, sizeof(value)) == 0 && strcmp(value, "no") == 0;
}

static void saved(int rc)
{
    copy_string(status, sizeof(status), rc < 0 ? strerror(rc) : "saved to ~/.bdeskui.conf");
}

/* A tiny desktop drawn by that skin itself: its background, a window, an icon in
 * its style and its taskbar's colours. x, y are absolute layout coordinates. */
static void preview(const struct skin *s, int x, int y, int w, int h)
{
    const struct skin *was = skin;
    struct frame_info f;
    int strip = 6, top = s->taskbar_top ? strip : 0, bar = s->taskbar_top ? 0 : h - strip;

    skin = s;
    gfx_clip(x, y, w, h);
    gfx_fill(0, 0, w, h, s->background);
    memset(&f, 0, sizeof(f));
    f.x = 5;
    f.y = top + 4;
    f.w = w - 34;
    f.h = h + 8; /* runs on under the taskbar */
    f.title_x = f.x + s->frame;
    f.title_y = f.y + s->frame;
    f.title_w = f.w - 2 * s->frame;
    f.content_x = f.title_x;
    f.content_y = f.title_y + s->title_h + s->gap;
    f.content_w = f.title_w;
    f.content_h = f.y + f.h - s->frame - f.content_y;
    for (int i = 0; i < 3; i++)
        f.button_x[i] = f.title_x + f.title_w - (3 - i) * s->title_button_w;
    f.title = s->name;
    f.icon = ICON_TERMINAL;
    f.focused = 1;
    s->draw_window(&f);
    gfx_fill(f.content_x, f.content_y, f.content_w, f.content_h, s->window);
    gfx_text(f.content_x + 3, f.content_y + 2, ">> _", s->text);
    gfx_icon(w - 22, top + (h - strip - 16) / 2, ICON_FILES, 1);
    gfx_fill(0, bar, w, strip, s->chrome);
    gfx_fill(0, s->taskbar_top ? bar + strip - 1 : bar, w, 1, s->line);
    gfx_fill(3, bar + 1, 12, strip - 2, s->accent);
    skin = was;
}

static void settings_draw(int instance, int w, int h, int focused)
{
    int y = 6, card_w = w - 2 * LEFT, preview_w = 150, ox, oy;

    (void)instance, (void)focused;
    gfx_origin(&ox, &oy);
    gfx_text_bold(LEFT, y, "Skin", skin->text);
    y += 12;
    for (int i = 0; i < skin_count; i++) {
        const struct skin *s = skins[i];
        int cy = y + i * (CARD_H + CARD_GAP);

        gfx_fill(LEFT, cy, card_w, CARD_H, s == skin ? skin->select : skin->field);
        gfx_outline(LEFT, cy, card_w, CARD_H, skin->rule);
        preview(s, ox + LEFT + 2, oy + cy + 2, preview_w - 4, CARD_H - 4);
        gfx_clip(ox, oy, w, h); /* back inside the window */
        gfx_text_bold(LEFT + preview_w + 8, cy + 8, s->name, skin->text);
        gfx_text_n(LEFT + preview_w + 8, cy + 20, s->blurb, (card_w - preview_w - 12) / CHAR_W, skin->dim);
        if (s == skin)
            gfx_text(LEFT + card_w - 7 * CHAR_W, cy + 8, "in use", skin->text);
    }
    y += skin_count * (CARD_H + CARD_GAP) + 2;
    gfx_text_bold(LEFT, y + 5, "Mouse", skin->text);
    for (int s = 1; s <= 3; s++) {
        char label[3] = {(char)('0' + s), 'x', 0};

        gfx_button(LEFT + 60 + (s - 1) * 40, y, 34, 17, label, s == mouse_speed);
    }
    y += 23;
    if (text_login_row) { /* after a text login, start BDeskUI or stay in the TTY */
        gfx_text_bold(LEFT, y + 5, "Text login", skin->text);
        gfx_button(LEFT + 84, y, 70, 17, "BDeskUI", autostart);
        gfx_button(LEFT + 160, y, 70, 17, "TTY", !autostart);
        y += 24;
    }
    gfx_text(LEFT, y, status, skin->dim);
}

static void settings_press(int instance, int x, int y, int w, int h)
{
    int top = 18;

    (void)instance, (void)h;
    for (int i = 0; i < skin_count; i++) {
        if (gfx_inside(x, y, LEFT, top + i * (CARD_H + CARD_GAP), w - 2 * LEFT, CARD_H)) {
            skin = skins[i];
            saved(bdeskui_conf_set("skin", skins[i]->name));
            return;
        }
    }
    top += skin_count * (CARD_H + CARD_GAP) + 2;
    for (int s = 1; s <= 3; s++) {
        if (gfx_inside(x, y, LEFT + 60 + (s - 1) * 40, top, 34, 17)) {
            char value[2] = {(char)('0' + s), 0};

            mouse_speed = s;
            saved(bdeskui_conf_set("mouse_speed", value));
            return;
        }
    }
    top += 23;
    if (text_login_row && (gfx_inside(x, y, LEFT + 84, top, 70, 17) || gfx_inside(x, y, LEFT + 160, top, 70, 17))) {
        autostart = x < LEFT + 160;
        saved(bdeskui_conf_set("autostart", autostart ? "yes" : "no"));
    }
}

const struct app app_settings = {
    .name = "BSconf", .blurb = "skins, mouse, login", .icon = ICON_SETTINGS,
    .default_w = 330, .default_h = 290, .min_w = 280, .min_h = 270,
    .draw = settings_draw, .press = settings_press,
};
