/*
 * Skins. Each is a whole look: its own icon style, window chrome, taskbar
 * (and where it sits), tiles, buttons, pointer, colours and a solid background.
 * The window manager decides where things go; skins only draw them.
 */
#include "bdeskui.h"

static void center_text(int x, int w, int y, const char *s, unsigned int color)
{
    gfx_text(x + (w - (int)strlen(s) * CHAR_W) / 2, y, s, color);
}

static void upper(char *out, unsigned int size, const char *s)
{
    unsigned int i = 0;

    for (; s[i] && i + 1 < size; i++)
        out[i] = s[i] >= 'a' && s[i] <= 'z' ? (char)(s[i] - 32) : s[i];
    out[i] = 0;
}

/* The three title glyphs, line-drawn, centred in a w x h box. 0 V, 1 ^, 2 X. */
static void glyph_box(int x, int y, int w, int h, int which, int thickness, unsigned int color)
{
    int cx = x + w / 2, cy = y + h / 2, s = h / 4 + 1;

    if (which == 0) {
        gfx_line(cx - s, cy - s / 2 - 1, cx, cy + s / 2, thickness, color);
        gfx_line(cx, cy + s / 2, cx + s, cy - s / 2 - 1, thickness, color);
    } else if (which == 1) {
        gfx_line(cx - s, cy + s / 2, cx, cy - s / 2 - 1, thickness, color);
        gfx_line(cx, cy - s / 2 - 1, cx + s, cy + s / 2, thickness, color);
    } else {
        gfx_line(cx - s, cy - s, cx + s - 1, cy + s - 1, thickness, color);
        gfx_line(cx + s - 1, cy - s, cx - s, cy + s - 1, thickness, color);
    }
}

/* Name, maybe cut to fit width w. */
static void fit_text(int x, int y, int w, const char *s, unsigned int color, int bold)
{
    char text[32];
    int fits = w / (CHAR_W + bold);

    copy_string(text, sizeof(text), s);
    if (fits < (int)sizeof(text) && fits >= 0)
        text[fits] = 0;
    if (bold)
        gfx_text_bold(x, y, text, color);
    else
        gfx_text(x, y, text, color);
}

/* ==== night: the BDeskUI mockup's language, compact ================================ */

static void night_window(const struct frame_info *f)
{
    const struct skin *s = skin;
    unsigned int text = f->focused ? s->chrome_text : s->idle;

    gfx_shadow(f->x, f->y, f->w, f->h, 5, 150);
    gfx_fill(f->x, f->y, f->w, f->h, s->chrome);
    gfx_chrome(f->title_x, f->title_y, f->title_w, s->title_h, f->focused ? LIGHTER(s->chrome, 16) : s->chrome, 0);
    fit_text(f->title_x + 6, f->title_y + (s->title_h - 7) / 2, f->button_x[0] - f->title_x - 10, f->title, text, 1);
    for (int i = 0; i < 3; i++) {
        gfx_fill(f->button_x[i], f->title_y, 2, s->title_h, s->line);
        glyph_box(f->button_x[i] + 2, f->title_y, s->title_button_w - 2, s->title_h, i, 2,
                  i == 2 && f->focused ? s->accent : text);
    }
    gfx_fill(f->x, f->content_y - s->gap, f->w, s->gap, s->line);
    gfx_frame(f->x, f->y, f->w, f->h);
}

static void night_taskbar(const struct bar_info *b)
{
    const struct skin *s = skin;
    int h = s->taskbar_h, box = h - 6;

    gfx_chrome(0, b->y, b->width, h, s->chrome, 0);
    gfx_fill(0, b->y, b->width, 2, s->line);
    gfx_chrome(3, b->y + 4, box, box, s->chrome, b->menu_open);
    gfx_frame(3, b->y + 4, box, box);
    gfx_text_scaled(3 + (box - 10) / 2, b->y + 4 + (box - 14) / 2, "B", 2, s->accent);
    for (int i = 0; i < b->count; i++) {
        int x = b->entries[i].x, y = b->y + 5, eh = h - 8;
        unsigned int text = b->entries[i].minimized ? s->idle : s->chrome_text;

        gfx_chrome(x, y, b->entries[i].w, eh, b->entries[i].active ? LIGHTER(s->chrome, 30) : s->chrome,
                   b->entries[i].active);
        fit_text(x + 5, y + (eh - 7) / 2, b->entries[i].name_w - 8, b->entries[i].name, text, 1);
        for (int k = 0; k < 2; k++) {
            int bx = x + b->entries[i].name_w + k * s->title_button_w;

            gfx_fill(bx, y, 2, eh, s->line);
            glyph_box(bx + 2, y, s->title_button_w - 2, eh, k + 1, 2, text);
        }
        gfx_frame(x, y, b->entries[i].w, eh);
    }
    gfx_text_scaled(b->width - 5 * 2 * CHAR_W - 6, b->y + (h - 14) / 2 + 1, b->clock, 2, s->chrome_text);
    gfx_text(b->width - 5 * 2 * CHAR_W - 14 - (int)strlen(b->address) * CHAR_W, b->y + (h - 7) / 2 + 1, b->address,
             s->idle);
}

static void night_tile(int x, int y, int size, enum icon icon, const char *label, int selected)
{
    gfx_chrome(x, y, size, size, skin->tile, 0);
    if (selected)
        gfx_frame(x - 2, y - 2, size + 4, size + 4);
    gfx_icon(x + (size - 32) / 2, y + (size - 32) / 2, icon, 2);
    center_text(x - 10, size + 20, y + size + 4, label, skin->chrome_text);
}

static void night_panel(int x, int y, int w, int h, const char *title)
{
    const struct skin *s = skin;

    gfx_shadow(x, y, w, h, 5, 150);
    gfx_fill(x, y, w, h, s->chrome);
    gfx_chrome(x + 2, y + 2, w - 4, panel_title_h() - 4, s->chrome, 0);
    gfx_text_bold(x + 8, y + (panel_title_h() - 7) / 2, title, s->accent);
    gfx_fill(x, y + panel_title_h() - 2, w, 2, s->line);
    gfx_frame(x, y, w, h);
}

static void night_button(int x, int y, int w, int h, const char *label, int hot)
{
    gfx_chrome(x, y, w, h, hot ? skin->select : skin->button, 0);
    gfx_outline(x, y, w, h, skin->line);
    center_text(x, w, y + (h - 7) / 2, label, skin->text);
}

static void night_field(int x, int y, int w, int h, int focused)
{
    gfx_fill(x, y, w, h, skin->field);
    gfx_outline(x, y, w, h, focused ? skin->accent : skin->line);
}

/* ==== aqua: frutiger aero, glossy gel and glass =================================== */

static void aqua_window(const struct frame_info *f)
{
    const struct skin *s = skin;
    unsigned int base = f->focused ? s->chrome : mix(s->chrome, 0xA0A8B0, 150);
    static const unsigned int gels[3] = {0x5AA8E0, 0x5AA8E0, 0xE8583A};

    gfx_shadow(f->x, f->y, f->w, f->h, 4, 110);
    gfx_gel(f->x, f->y, f->w, f->h, base, 0); /* the glass border */
    gfx_gel(f->title_x, f->title_y, f->title_w, s->title_h, base, 0);
    gfx_text_raised(f->title_x + (f->title_w - (int)strlen(f->title) * (CHAR_W + 1)) / 2,
                    f->title_y + (s->title_h - 7) / 2, f->title, s->chrome_text);
    gfx_icon(f->title_x + 3, f->title_y + (s->title_h - 16) / 2, f->icon, 1);
    for (int i = 0; i < 3; i++) {
        int bx = f->button_x[i] + 2, by = f->title_y + 3, bw = s->title_button_w - 4, bh = s->title_h - 6;

        gfx_slab(bx, by, bw, bh, f->focused ? gels[i] : 0xB8C4CC, 0);
        if (i == 0)
            gfx_fill(bx + bw / 2 - 3, by + bh / 2, 6, 2, 0xFFFFFF);
        else if (i == 1) {
            gfx_fill(bx + bw / 2 - 3, by + bh / 2 - 1, 7, 2, 0xFFFFFF);
            gfx_fill(bx + bw / 2 - 1, by + bh / 2 - 3, 2, 7, 0xFFFFFF);
        } else {
            gfx_line(bx + bw / 2 - 3, by + bh / 2 - 3, bx + bw / 2 + 2, by + bh / 2 + 2, 2, 0xFFFFFF);
            gfx_line(bx + bw / 2 + 2, by + bh / 2 - 3, bx + bw / 2 - 3, by + bh / 2 + 2, 2, 0xFFFFFF);
        }
    }
    gfx_outline(f->x, f->y, f->w, f->h, s->line);
    gfx_outline(f->content_x - 1, f->content_y - 1, f->content_w + 2, f->content_h + 2, DARKER(base, 120));
}

static void aqua_taskbar(const struct bar_info *b)
{
    const struct skin *s = skin;
    int h = s->taskbar_h, r = (h - 6) / 2;

    gfx_chrome(0, b->y, b->width, h, s->chrome, 0);
    gfx_fill(0, b->y, b->width, 1, s->line);
    /* the orb */
    gfx_disc(4 + r + 8, b->y + h / 2, r + 1, s->line);
    gfx_disc(4 + r + 8, b->y + h / 2, r, b->menu_open ? DARKER(s->accent, 60) : s->accent);
    gfx_disc(4 + r + 8, b->y + h / 2 - r / 2, r / 2, LIGHTER(s->accent, 150));
    gfx_text_raised(4 + r + 8 - 6, b->y + h / 2 - 3, "BS", 0xFFFFFF);
    for (int i = 0; i < b->count; i++) {
        int x = b->entries[i].x, y = b->y + 4;

        gfx_slab(x, y, b->entries[i].w, h - 8, b->entries[i].active ? 0x9ED8F6 : LIGHTER(s->chrome, 40),
                 b->entries[i].active);
        gfx_icon(x + 4, y + (h - 8 - 16) / 2, b->entries[i].icon, 1);
        fit_text(x + 24, y + (h - 8 - 7) / 2, b->entries[i].w - 28, b->entries[i].name,
                 b->entries[i].minimized ? 0x30485A : INK, 0);
    }
    gfx_text_raised(b->width - 5 * (CHAR_W + 1) - 10, b->y + (h - 7) / 2, b->clock, 0xFFFFFF);
    gfx_text(b->width - 5 * (CHAR_W + 1) - 20 - (int)strlen(b->address) * CHAR_W, b->y + (h - 7) / 2, b->address,
             LIGHTER(s->chrome, 160));
}

static void aqua_tile(int x, int y, int size, enum icon icon, const char *label, int selected)
{
    if (selected) {
        gfx_darken(x - 6, y - 4, size + 12, size + 20, 60);
        gfx_outline(x - 6, y - 4, size + 12, size + 20, 0xFFFFFF);
    }
    gfx_icon(x + (size - 32) / 2, y + (size - 32) / 2, icon, 2);
    center_text(x - 9, size + 20, y + size + 4, label, INK);
    center_text(x - 10, size + 20, y + size + 3, label, 0xFFFFFF);
}

static void aqua_panel(int x, int y, int w, int h, const char *title)
{
    gfx_shadow(x, y, w, h, 4, 110);
    gfx_gel(x, y, w, h, skin->chrome, 0);
    gfx_fill(x + 3, y + panel_title_h(), w - 6, h - panel_title_h() - 3, skin->window);
    gfx_text_raised(x + (w - (int)strlen(title) * (CHAR_W + 1)) / 2, y + (panel_title_h() - 7) / 2, title, 0xFFFFFF);
    gfx_outline(x, y, w, h, skin->line);
}

static void aqua_button(int x, int y, int w, int h, const char *label, int hot)
{
    gfx_slab(x, y, w, h, hot ? skin->select : skin->button, 0);
    center_text(x, w, y + (h - 7) / 2, label, INK);
}

static void aqua_field(int x, int y, int w, int h, int focused)
{
    gfx_inset(x, y, w, h, skin->field);
    if (focused)
        gfx_outline(x - 2, y - 2, w + 4, h + 4, 0x46B8F0);
}

/* ==== dos: a modern DOS, text-mode boxes ========================================== */

/* A double-line box, the way text-mode programs draw them. */
static void double_box(int x, int y, int w, int h, unsigned int color)
{
    gfx_outline(x + 1, y + 1, w - 2, h - 2, color);
    gfx_outline(x + 3, y + 3, w - 6, h - 6, color);
}

static void dos_window(const struct frame_info *f)
{
    const struct skin *s = skin;
    char title[40];
    int tw;
    unsigned int border = f->focused ? s->line : s->idle;

    gfx_shadow(f->x, f->y, f->w, f->h, 6, 220); /* text-mode shadow: almost black */
    gfx_fill(f->x, f->y, f->w, f->h, s->window);
    double_box(f->x, f->y, f->w, f->h, border);
    title[0] = '[';
    title[1] = ' ';
    copy_string(title + 2, sizeof(title) - 4, f->title);
    copy_string(title + strlen(title), 3, " ]");
    tw = (int)strlen(title) * CHAR_W + 2;
    gfx_fill(f->x + (f->w - tw) / 2, f->y, tw, 9, f->focused ? s->chrome : s->window);
    gfx_text(f->x + (f->w - tw) / 2 + 1, f->y + 1, title, f->focused ? s->chrome_text : s->idle);
    for (int i = 0; i < 3; i++) {
        static const char *const labels[3] = {"[v]", "[^]", "[x]"};

        gfx_fill(f->button_x[i], f->y, s->title_button_w, 9, s->window);
        gfx_text(f->button_x[i], f->y + 1, labels[i], i == 2 && f->focused ? s->accent : border);
    }
}

static void dos_taskbar(const struct bar_info *b)
{
    const struct skin *s = skin;

    gfx_fill(0, b->y, b->width, s->taskbar_h, s->chrome);
    gfx_fill(0, b->y, s->launcher_w, s->taskbar_h, b->menu_open ? s->select : 0x000000);
    gfx_text(3, b->y + 2, "\x2a BS", s->accent);
    for (int i = 0; i < b->count; i++) {
        char text[24];
        int x = b->entries[i].x;

        text[0] = (char)('1' + i);
        text[1] = ' ';
        copy_string(text + 2, sizeof(text) - 2, b->entries[i].name);
        if (b->entries[i].active)
            gfx_fill(x, b->y, b->entries[i].w, s->taskbar_h, s->select);
        fit_text(x + 3, b->y + 2, b->entries[i].w - 4, text, b->entries[i].minimized ? s->idle : s->chrome_text, 0);
    }
    gfx_text(b->width - 5 * CHAR_W - 4, b->y + 2, b->clock, s->chrome_text);
    gfx_text(b->width - 5 * CHAR_W - 12 - (int)strlen(b->address) * CHAR_W, b->y + 2, b->address, s->idle);
}

static void dos_tile(int x, int y, int size, enum icon icon, const char *label, int selected)
{
    int w = (int)strlen(label) * CHAR_W + 4;

    gfx_icon(x + (size - 32) / 2, y + (size - 32) / 2, icon, 2);
    gfx_fill(x + (size - w) / 2, y + size + 2, w, 10, selected ? skin->chrome : skin->background);
    center_text(x, size, y + size + 3, label, selected ? skin->chrome_text : 0xFFFFFF);
}

static void dos_panel(int x, int y, int w, int h, const char *title)
{
    char text[40];
    int tw;

    gfx_shadow(x, y, w, h, 6, 220);
    gfx_fill(x, y, w, h, skin->window);
    double_box(x, y, w, h, skin->line);
    text[0] = '[';
    text[1] = ' ';
    copy_string(text + 2, sizeof(text) - 4, title);
    copy_string(text + strlen(text), 3, " ]");
    tw = (int)strlen(text) * CHAR_W + 2;
    gfx_fill(x + (w - tw) / 2, y, tw, 9, skin->chrome);
    gfx_text(x + (w - tw) / 2 + 1, y + 1, text, skin->chrome_text);
}

static void dos_button(int x, int y, int w, int h, const char *label, int hot)
{
    char text[32];

    unsigned int n = strlen(label);

    if ((int)(n + 4) * CHAR_W <= w - 2) { /* "< label >", or as much of it as fits */
        text[0] = '<';
        text[1] = ' ';
        copy_string(text + 2, sizeof(text) - 4, label);
        copy_string(text + strlen(text), 3, " >");
    } else if ((int)(n + 2) * CHAR_W <= w - 2) {
        text[0] = '<';
        copy_string(text + 1, sizeof(text) - 2, label);
        copy_string(text + strlen(text), 2, ">");
    } else {
        copy_string(text, sizeof(text), label);
    }
    if (hot)
        gfx_fill(x, y, w, h, skin->select);
    center_text(x, w, y + (h - 7) / 2, text, hot ? 0x000000 : skin->text);
}

static void dos_field(int x, int y, int w, int h, int focused)
{
    gfx_fill(x, y, w, h, skin->field);
    gfx_fill(x, y + h - 1, w, 1, focused ? skin->accent : skin->idle);
}

/* ==== concrete: brutalist, paper and ink ========================================== */

static void concrete_window(const struct frame_info *f)
{
    const struct skin *s = skin;
    char title[40];

    gfx_fill(f->x + 8, f->y + 8, f->w, f->h, 0x111111); /* a solid shadow, not a tint */
    gfx_fill(f->x, f->y, f->w, f->h, s->line);
    gfx_fill(f->title_x, f->title_y, f->title_w, s->title_h, f->focused ? 0x111111 : 0x5A5A5A);
    upper(title, sizeof(title), f->title);
    fit_text(f->title_x + 6, f->title_y + (s->title_h - 7) / 2, f->button_x[0] - f->title_x - 10, title,
             f->focused ? 0xFFFFFF : 0xCFCFCF, 1);
    for (int i = 0; i < 3; i++) {
        int bx = f->button_x[i];

        gfx_fill(bx, f->title_y, s->title_button_w, s->title_h, i == 2 && f->focused ? s->accent : s->chrome);
        gfx_fill(bx, f->title_y, 3, s->title_h, s->line);
        glyph_box(bx + 3, f->title_y, s->title_button_w - 3, s->title_h, i, 2, 0x111111);
    }
}

static void concrete_taskbar(const struct bar_info *b)
{
    const struct skin *s = skin;
    int h = s->taskbar_h;

    gfx_fill(0, b->y, b->width, h, s->chrome);
    gfx_fill(0, b->y, b->width, 3, s->line);
    gfx_fill(3, b->y + 6, s->launcher_w - 6, h - 9, b->menu_open ? s->accent : 0x111111);
    gfx_text_bold(3 + (s->launcher_w - 6 - 13) / 2, b->y + 6 + (h - 9 - 7) / 2, "BS",
                  b->menu_open ? 0x111111 : s->accent);
    for (int i = 0; i < b->count; i++) {
        char name[24];
        int x = b->entries[i].x, y = b->y + 6, eh = h - 9;

        gfx_fill(x, y, b->entries[i].w, eh, 0x111111);
        gfx_fill(x + 2, y + 2, b->entries[i].w - 4, eh - 4, b->entries[i].active ? s->select : s->chrome);
        upper(name, sizeof(name), b->entries[i].name);
        fit_text(x + 6, y + (eh - 7) / 2, b->entries[i].name_w - 8, name, b->entries[i].minimized ? s->idle : 0x111111, 1);
        for (int k = 0; k < 2; k++) {
            int bx = x + b->entries[i].name_w + k * s->title_button_w;

            gfx_fill(bx, y, 2, eh, 0x111111);
            glyph_box(bx + 2, y, s->title_button_w - 2, eh, k + 1, 2, 0x111111);
        }
    }
    gfx_text_bold(b->width - 5 * (CHAR_W + 1) - 8, b->y + (h - 7) / 2 + 1, b->clock, 0x111111);
    gfx_text(b->width - 5 * (CHAR_W + 1) - 16 - (int)strlen(b->address) * CHAR_W, b->y + (h - 7) / 2 + 1, b->address,
             s->idle);
}

static void concrete_tile(int x, int y, int size, enum icon icon, const char *label, int selected)
{
    char name[24];

    gfx_fill(x, y, size, size, 0x111111);
    gfx_fill(x + 3, y + 3, size - 6, size - 6, selected ? skin->select : skin->tile);
    gfx_icon(x + (size - 32) / 2, y + (size - 32) / 2, icon, 2);
    upper(name, sizeof(name), label);
    gfx_fill(x - 4, y + size + 1, size + 8, 11, 0x111111);
    center_text(x - 4, size + 8, y + size + 3, name, 0xFFFFFF);
}

static void concrete_panel(int x, int y, int w, int h, const char *title)
{
    char text[40];

    gfx_fill(x + 8, y + 8, w, h, 0x111111);
    gfx_fill(x, y, w, h, 0x111111);
    gfx_fill(x + 3, y + panel_title_h(), w - 6, h - panel_title_h() - 3, skin->window);
    upper(text, sizeof(text), title);
    gfx_text_bold(x + 8, y + (panel_title_h() - 7) / 2, text, 0xFFFFFF);
}

static void concrete_button(int x, int y, int w, int h, const char *label, int hot)
{
    gfx_fill(x, y, w, h, 0x111111);
    gfx_fill(x + 2, y + 2, w - 4, h - 4, hot ? skin->accent : skin->button);
    center_text(x, w, y + (h - 7) / 2, label, 0x111111);
}

static void concrete_field(int x, int y, int w, int h, int focused)
{
    gfx_fill(x, y, w, h, focused ? skin->accent : 0x111111);
    gfx_fill(x + 2, y + 2, w - 4, h - 4, skin->field);
}

/* ==== the list ======================================================================= */

static const struct skin night = {
    .name = "night", .blurb = "charcoal, white lines",
    .background = 0x12121C, .chrome = 0x181818, .chrome_text = 0xF0F0F0, .line = 0xE6E6E6, .accent = 0xFFD95E,
    .idle = 0x8A8A8A, .tile = 0x0C0C0C, .window = 0x000000, .text = 0xE2E2E2, .dim = 0x8A8A8A, .rule = 0x262626,
    .select = 0x2A4870, .field = 0x0E0E0E, .button = 0x262626, .gloss = 36,
    .frame = 4, .title_h = 18, .gap = 2, .title_button_w = 18, .taskbar_h = 28, .taskbar_top = 0,
    .launcher_w = 26, .task_buttons = 2, .tile_size = 40, .icons = ICONS_LINE, .pointer = POINTER_CLASSIC,
    .draw_window = night_window, .draw_taskbar = night_taskbar, .draw_tile = night_tile,
    .draw_panel = night_panel, .draw_button = night_button, .draw_field = night_field,
};

static const struct skin aqua = {
    .name = "aqua", .blurb = "glossy gel and glass",
    .background = 0x2F8AD0, .chrome = 0x2E86B0, .chrome_text = 0xFFFFFF, .line = 0x0B2234, .accent = 0x6CD83A,
    .idle = 0xC8E0EC, .tile = 0x2F8AD0, .window = 0xF2F7FA, .text = 0x10202A, .dim = 0x5C7484, .rule = 0xD0DDE4,
    .select = 0x8ED2F6, .field = 0xFFFFFF, .button = 0xD2E2EA, .gloss = 170,
    .frame = 4, .title_h = 20, .gap = 0, .title_button_w = 22, .taskbar_h = 30, .taskbar_top = 0,
    .launcher_w = 40, .task_buttons = 0, .tile_size = 0, .icons = ICONS_GLOSSY, .pointer = POINTER_CLASSIC,
    .draw_window = aqua_window, .draw_taskbar = aqua_taskbar, .draw_tile = aqua_tile,
    .draw_panel = aqua_panel, .draw_button = aqua_button, .draw_field = aqua_field,
};

static const struct skin dos = {
    .name = "dos", .blurb = "text-mode boxes, EGA",
    .background = 0x0000AA, .chrome = 0xAAAAAA, .chrome_text = 0x000000, .line = 0xFFFFFF, .accent = 0xFFFF55,
    .idle = 0x555555, .tile = 0x0000AA, .window = 0x000000, .text = 0xAAAAAA, .dim = 0x555555, .rule = 0x00002A,
    .select = 0x00AAAA, .field = 0x0000AA, .button = 0x000000, .gloss = 0,
    .frame = 6, .title_h = 4, .gap = 0, .title_button_w = 18, .taskbar_h = 11, .taskbar_top = 1,
    .launcher_w = 30, .task_buttons = 0, .tile_size = 36, .icons = ICONS_EGA, .pointer = POINTER_BLOCK,
    .draw_window = dos_window, .draw_taskbar = dos_taskbar, .draw_tile = dos_tile,
    .draw_panel = dos_panel, .draw_button = dos_button, .draw_field = dos_field,
};

static const struct skin concrete = {
    .name = "concrete", .blurb = "brutalist paper and ink",
    .background = 0xB9B9B3, .chrome = 0xF4F4F0, .chrome_text = 0x111111, .line = 0x111111, .accent = 0xFF5A1F,
    .idle = 0x777777, .tile = 0xF4F4F0, .window = 0xF4F4F0, .text = 0x111111, .dim = 0x666666, .rule = 0xDDDDD8,
    .select = 0xFFD2BC, .field = 0xFFFFFF, .button = 0xF4F4F0, .gloss = 0,
    .frame = 3, .title_h = 20, .gap = 3, .title_button_w = 22, .taskbar_h = 32, .taskbar_top = 0,
    .launcher_w = 42, .task_buttons = 2, .tile_size = 40, .icons = ICONS_PICTO, .pointer = POINTER_HEAVY,
    .draw_window = concrete_window, .draw_taskbar = concrete_taskbar, .draw_tile = concrete_tile,
    .draw_panel = concrete_panel, .draw_button = concrete_button, .draw_field = concrete_field,
};

const struct skin *const skins[] = {&night, &aqua, &dos, &concrete};
const int skin_count = 4;
const struct skin *skin = &night;

const struct skin *skin_named(const char *name)
{
    for (int i = 0; i < skin_count; i++)
        if (strcmp(skins[i]->name, name) == 0)
            return skins[i];
    return 0;
}

int panel_title_h(void)
{
    return skin == &dos ? 12 : skin->title_h + 4;
}
