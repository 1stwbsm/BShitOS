/*
 * Drawing. Everything lands in a 640x400 frame that the kernel scales onto the
 * screen. The look: gel and glass shading for depth, held in by 2 px black
 * frames and hard, unblurred shadows.
 */
#include <bshit/font5x7.h>

#include "bdeskui.h"

static unsigned int frame[SCREEN_W * SCREEN_H];
static unsigned int wallpaper[SCREEN_W * SCREEN_H];
static int origin_x, origin_y, clip_left, clip_top, clip_right = SCREEN_W, clip_bottom = SCREEN_H;

unsigned int mix(unsigned int a, unsigned int b, int amount)
{
    unsigned int r = (((a >> 16) & 255) * (unsigned int)(256 - amount) + ((b >> 16) & 255) * (unsigned int)amount) >> 8;
    unsigned int g = (((a >> 8) & 255) * (unsigned int)(256 - amount) + ((b >> 8) & 255) * (unsigned int)amount) >> 8;
    unsigned int bl = ((a & 255) * (unsigned int)(256 - amount) + (b & 255) * (unsigned int)amount) >> 8;

    return r << 16 | g << 8 | bl;
}

void gfx_clip(int x, int y, int w, int h)
{
    origin_x = x;
    origin_y = y;
    clip_left = x < 0 ? 0 : x;
    clip_top = y < 0 ? 0 : y;
    clip_right = x + w > SCREEN_W ? SCREEN_W : x + w;
    clip_bottom = y + h > SCREEN_H ? SCREEN_H : y + h;
}

void gfx_unclip(void)
{
    gfx_clip(0, 0, SCREEN_W, SCREEN_H);
}

/* Absolute-coordinate span clipped to the current clip; returns 0 if empty. */
static int span(int *left, int *top, int *right, int *bottom)
{
    if (*left < clip_left)
        *left = clip_left;
    if (*top < clip_top)
        *top = clip_top;
    if (*right > clip_right)
        *right = clip_right;
    if (*bottom > clip_bottom)
        *bottom = clip_bottom;
    return *left < *right && *top < *bottom;
}

static void plot(int x, int y, unsigned int color)
{
    if (x >= clip_left && x < clip_right && y >= clip_top && y < clip_bottom)
        frame[y * SCREEN_W + x] = color;
}

void gfx_fill(int x, int y, int w, int h, unsigned int color)
{
    int left = x + origin_x, top = y + origin_y, right = left + w, bottom = top + h;

    if (!span(&left, &top, &right, &bottom))
        return;
    for (int row = top; row < bottom; row++)
        for (int column = left; column < right; column++)
            frame[row * SCREEN_W + column] = color;
}

void gfx_gradient(int x, int y, int w, int h, unsigned int top, unsigned int bottom)
{
    for (int row = 0; row < h; row++)
        gfx_fill(x, y + row, w, 1, mix(top, bottom, h > 1 ? row * 256 / (h - 1) : 0));
}

void gfx_gel(int x, int y, int w, int h, unsigned int base, int pressed)
{
    int upper = h * 9 / 20;

    if (h < 3) {
        gfx_fill(x, y, w, h, base);
        return;
    }
    if (pressed) { /* lit from below: pushed in */
        gfx_gradient(x, y, w, h, DARKER(base, 70), LIGHTER(base, 60));
        gfx_fill(x, y, w, 1, DARKER(base, 120));
        return;
    }
    gfx_gradient(x, y, w, upper, LIGHTER(base, 170), LIGHTER(base, 70));
    gfx_gradient(x, y + upper, w, h - upper, DARKER(base, 10), LIGHTER(base, 40));
    gfx_fill(x + 1, y + 1, w - 2, 1, LIGHTER(base, 220)); /* the specular line */
    gfx_fill(x, y + h - 1, w, 1, LIGHTER(base, 90)); /* light coming back up through the gel */
}

void gfx_glass(int x, int y, int w, int h, unsigned int top, unsigned int bottom)
{
    gfx_gradient(x, y, w, h, top, bottom);
    gfx_fill(x, y, w, 1, LIGHTER(top, 150));
    gfx_gradient(x, y + 1, w, h / 2, LIGHTER(top, 50), top);
}

void gfx_bevel(int x, int y, int w, int h, int raised)
{
    unsigned int light = raised ? 0xFFFFFF : 0x7A8A94, dark = raised ? 0x7A8A94 : 0xFFFFFF;

    gfx_fill(x, y, w, 1, light);
    gfx_fill(x, y, 1, h, light);
    gfx_fill(x, y + h - 1, w, 1, dark);
    gfx_fill(x + w - 1, y, 1, h, dark);
}

void gfx_inset(int x, int y, int w, int h, unsigned int fill)
{
    gfx_fill(x, y, w, h, fill);
    gfx_fill(x, y, w, 2, DARKER(fill, 90));
    gfx_fill(x, y, 2, h, DARKER(fill, 60));
    gfx_fill(x, y + h - 1, w, 1, LIGHTER(fill, 120));
    gfx_outline(x - 1, y - 1, w + 2, h + 2, INK);
}

void gfx_outline(int x, int y, int w, int h, unsigned int color)
{
    gfx_fill(x, y, w, 1, color);
    gfx_fill(x, y + h - 1, w, 1, color);
    gfx_fill(x, y, 1, h, color);
    gfx_fill(x + w - 1, y, 1, h, color);
}

void gfx_frame(int x, int y, int w, int h)
{
    gfx_outline(x, y, w, h, INK);
    gfx_outline(x + 1, y + 1, w - 2, h - 2, INK);
}

void gfx_darken(int x, int y, int w, int h, int amount)
{
    int left = x + origin_x, top = y + origin_y, right = left + w, bottom = top + h;

    if (!span(&left, &top, &right, &bottom))
        return;
    for (int row = top; row < bottom; row++)
        for (int column = left; column < right; column++)
            frame[row * SCREEN_W + column] = mix(frame[row * SCREEN_W + column], 0x001018, amount);
}

void gfx_shadow(int x, int y, int w, int h)
{
    gfx_darken(x + 5, y + h, w, 5, 150);
    gfx_darken(x + w, y + 5, 5, h - 5, 150);
}

void gfx_slab(int x, int y, int w, int h, unsigned int base, int pressed)
{
    gfx_gel(x + 1, y + 1, w - 2, h - 2, base, pressed);
    /* The frame skips the corner pixels, and the cut closes diagonally inside. */
    gfx_fill(x + 1, y, w - 2, 1, INK);
    gfx_fill(x + 1, y + h - 1, w - 2, 1, INK);
    gfx_fill(x, y + 1, 1, h - 2, INK);
    gfx_fill(x + w - 1, y + 1, 1, h - 2, INK);
    plot(origin_x + x + 1, origin_y + y + 1, INK);
    plot(origin_x + x + w - 2, origin_y + y + 1, INK);
    plot(origin_x + x + 1, origin_y + y + h - 2, INK);
    plot(origin_x + x + w - 2, origin_y + y + h - 2, INK);
}

/* ---- Text ------------------------------------------------------------------- */

static void glyph(int x, int y, char c, int scale, unsigned int color)
{
    const unsigned char *g;

    if (c <= 0x20 || c > 0x7E)
        return;
    g = bs_font5x7[c - 0x20];
    for (int column = 0; column < 5; column++)
        for (int row = 0; row < 7; row++)
            if (g[column] & (1 << row))
                for (int sy = 0; sy < scale; sy++)
                    for (int sx = 0; sx < scale; sx++)
                        plot(origin_x + x + column * scale + sx, origin_y + y + row * scale + sy, color);
}

void gfx_text_n(int x, int y, const char *s, int count, unsigned int color)
{
    for (int i = 0; i < count && s[i]; i++)
        glyph(x + i * CHAR_W, y, s[i], 1, color);
}

void gfx_text(int x, int y, const char *s, unsigned int color)
{
    gfx_text_n(x, y, s, 0x7FFFFFFF, color);
}

void gfx_text_bold(int x, int y, const char *s, unsigned int color)
{
    for (int i = 0; s[i]; i++) {
        glyph(x + i * (CHAR_W + 1), y, s[i], 1, color);
        glyph(x + i * (CHAR_W + 1) + 1, y, s[i], 1, color);
    }
}

void gfx_text_raised(int x, int y, const char *s, unsigned int color)
{
    gfx_text_bold(x + 1, y + 1, s, INK);
    gfx_text_bold(x, y, s, color);
}

void gfx_text_scaled(int x, int y, const char *s, int scale, unsigned int color)
{
    for (; *s; s++, x += CHAR_W * scale)
        glyph(x, y, *s, scale, color);
}

/* ---- Controls ---------------------------------------------------------------- */

void gfx_button(int x, int y, int w, int h, const char *label, int highlighted)
{
    unsigned int base = highlighted ? 0x46B8F0 : 0xC9D6DE;

    gfx_slab(x, y, w, h, base, 0);
    gfx_text(x + (w - (int)strlen(label) * CHAR_W) / 2, y + (h - 7) / 2, label, highlighted ? INK : 0x18242C);
}

/* A liquid tube: a recessed well with gel filling it up. */
void gfx_bar(int x, int y, int w, int h, unsigned int used, unsigned int total, unsigned int color)
{
    int filled;

    while (total > (1u << 20)) { /* keep the product inside 32 bits */
        total >>= 1;
        used >>= 1;
    }
    filled = total ? (int)(used * (unsigned int)(w - 2) / total) : 0;
    gfx_inset(x, y, w, h, 0xDDE6EB);
    if (filled > 0)
        gfx_gel(x + 1, y + 1, filled, h - 2, color, 0);
}

int gfx_inside(int px, int py, int x, int y, int w, int h)
{
    return px >= x && py >= y && px < x + w && py < y + h;
}

/* ---- Icons: 16x16 maps. Shading gets a gloss over the top rows. --------------- */

static const char *const icon_maps[ICON_COUNT][16] = {
    [ICON_TERMINAL] = {
        "................", ".KKKKKKKKKKKKKK.", ".KwwwwwwwwwwwwK.", ".KwKKKKKKKKKKwK.",
        ".KwKddddddddKwK.", ".KwKdGddddddKwK.", ".KwKddGdddddKwK.", ".KwKdGdddGGdKwK.",
        ".KwKddddddddKwK.", ".KwKKKKKKKKKKwK.", ".KwwwwwwwwwwwwK.", ".KKKKKKKKKKKKKK.",
        "......KggK......", "....KKKKKKKK....", "....KwwwwwwK....", "....KKKKKKKK....",
    },
    [ICON_FILES] = {
        "................", "................", ".KKKKK..........", "KYYYYYK.........",
        "KYWWWYKKKKKKKK..", "KYYYYYYYYYYYYYK.", "KKKKKKKKKKKKKKKK", "KOYYYYYYYYYYYYOK",
        "KOYWWWWWWWWWWYOK", "KOYYYYYYYYYYYYOK", "KOYYYYYYYYYYYYOK", "KOOYYYYYYYYYYOOK",
        "KOOOOOOOOOOOOOOK", ".KKKKKKKKKKKKKK.", "................", "................",
    },
    [ICON_NOTES] = {
        "..KKKKKKKKKKKK..", "..KBBBBBBBBBBK..", "..KKKKKKKKKKKK..", "..KWWWWWWWWWWK..",
        "..KWggggggggWK..", "..KWWWWWWWWWWK..", "..KWggggggWWWK..", "..KWWWWWWWWWWK..",
        "..KWggggggggWK..", "..KWWWWWWWWWWK..", "..KWgggggWWWWK..", "..KWWWWWWWWWWK..",
        "..KwwwwwwwwwwK..", "..KKKKKKKKKKKK..", "................", "................",
    },
    [ICON_WEB] = {
        ".....KKKKKK.....", "...KKBBSSBBKK...", "..KBBBGGBBBBBK..", ".KBBGGGGBBBSBBK.",
        ".KBGGGGGBBBBBBK.", "KBBGGGGBBBGGBBBK", "KBBBGGBBBGGGGBBK", "KBBBBGBBBGGGGGBK",
        "KBBBBBBBBBGGGBBK", "KBBBBBBBBBGGBBBK", ".KBBBGGBBBBBBBK.", ".KBBGGGGBBBBBBK.",
        "..KBBGGBBBBBBK..", "...KKBBBBBBKK...", ".....KKKKKK.....", "................",
    },
    [ICON_SYSTEM] = {
        "................", "....KKKKKKKK....", "..KKwwwwwwwwKK..", ".KwwRwwwwwwGwwK.",
        ".KwRwwwwwwwwGwK.", "KwwwwwwwwwwKwwwK", "KwwwwwwwwwKwwwwK", "KwwwwwwwwKwwwwwK",
        "KwwwwwwwKKwwwwwK", "KwwwwwwKKKKwwwwK", "KKKKKKKKKKKKKKKK", "KddddddddddddddK",
        "KKKKKKKKKKKKKKKK", "................", "................", "................",
    },
    [ICON_SETTINGS] = {
        "................", ".KKKKKKKKKKKKKK.", ".KwwwwwwwwwwwwK.", ".KwgggKKKggggwK.",
        ".KwgggKRKggggwK.", ".KwgggKKKggggwK.", ".KwwwwwwwwwwwwK.", ".KwggggggKKKgwK.",
        ".KwggggggKGKgwK.", ".KwggggggKKKgwK.", ".KwwwwwwwwwwwwK.", ".KwgKKKggggggwK.",
        ".KwgKBKggggggwK.", ".KwgKKKggggggwK.", ".KwwwwwwwwwwwwK.", ".KKKKKKKKKKKKKK.",
    },
    [ICON_CUBE] = {
        "................", ".......KK.......", ".....KKLLKK.....", "...KKLLLLLLKK...",
        ".KKLLLLLLLLLLKK.", ".KAALLLLLLLLDDK.", ".KAAAALLLLDDDDK.", ".KAAAAAKKDDDDDK.",
        ".KAAAAAAKDDDDDK.", ".KAAAAAAKDDDDDK.", ".KAAAAAAKDDDDDK.", ".KKAAAAAKDDDDKK.",
        "...KKAAAKDDKK...", ".....KKAKKK.....", ".......KK.......", "................",
    },
    [ICON_FOLDER] = {
        "................", "................", ".KKKKK..........", "KYYYYYK.........",
        "KYWWWYKKKKKKKK..", "KYYYYYYYYYYYYYK.", "KKKKKKKKKKKKKKKK", "KOYYYYYYYYYYYYOK",
        "KOYWWWWWWWWWWYOK", "KOYYYYYYYYYYYYOK", "KOYYYYYYYYYYYYOK", "KOOYYYYYYYYYYOOK",
        "KOOOOOOOOOOOOOOK", ".KKKKKKKKKKKKKK.", "................", "................",
    },
    [ICON_FILE] = {
        "..KKKKKKKK......", "..KWWWWWWKK.....", "..KWWWWWWKwK....", "..KWWWWWWKKKK...",
        "..KWWWWWWWWWK...", "..KWggggggWWK...", "..KWWWWWWWWWK...", "..KWgggggWWWK...",
        "..KWWWWWWWWWK...", "..KWggggggWWK...", "..KWWWWWWWWWK...", "..KWWWWWWWWWK...",
        "..KKKKKKKKKKK...", "................", "................", "................",
    },
    [ICON_PROGRAM] = {
        "................", ".KKKKKKKKKKKKKK.", ".KAAAAAAAAAKRRK.", ".KKKKKKKKKKKKKK.",
        ".KWWWWWWWWWWWWK.", ".KWggggWWWWWWWK.", ".KWWWWWWWWWWWWK.", ".KWgggggggWWWWK.",
        ".KWWWWWWWWWWWWK.", ".KWggggggWWWWWK.", ".KWWWWWWWWWWWWK.", ".KKKKKKKKKKKKKK.",
        "................", "................", "................", "................",
    },
    [ICON_POWER] = {
        ".....KKKKKK.....", "...KKRRRRRRKK...", "..KRRRRWWRRRRK..", ".KRRRRRWWRRRRRK.",
        ".KRRWRRWWRRWRRK.", "KRRWRRRWWRRRWRRK", "KRRWRRRWWRRRWRRK", "KRRWRRRRRRRRWRRK",
        "KRRWRRRRRRRRWRRK", "KRRRWRRRRRRWRRRK", ".KRRRWWWWWWRRRK.", ".KRRRRRRRRRRRRK.",
        "..KRRRRRRRRRRK..", "...KKRRRRRRKK...", ".....KKKKKK.....", "................",
    },
};

static const unsigned int icon_accents[ICON_COUNT] = {
    [ICON_CUBE] = 0x8E5BD8, [ICON_PROGRAM] = 0x2F86D8,
};

static int icon_color(char c, unsigned int accent, unsigned int *out)
{
    switch (c) {
    case 'K': *out = INK; return 1;
    case 'W': *out = 0xFFFFFF; return 1;
    case 'w': *out = 0xE4ECF1; return 1;
    case 'g': *out = 0x9AAAB4; return 1;
    case 'd': *out = 0x24343E; return 1;
    case 'Y': *out = 0xFFD447; return 1;
    case 'O': *out = 0xE89A1C; return 1;
    case 'B': *out = 0x2F86D8; return 1;
    case 'S': *out = 0xBCEBFF; return 1;
    case 'G': *out = 0x4DC24A; return 1;
    case 'R': *out = 0xE2412F; return 1;
    case 'A': *out = accent; return 1;
    case 'L': *out = LIGHTER(accent, 110); return 1;
    case 'D': *out = DARKER(accent, 80); return 1;
    default: return 0;
    }
}

void gfx_icon(int x, int y, enum icon which, int scale)
{
    const char *const *map = icon_maps[which];

    for (int row = 0; row < 16; row++) {
        for (int column = 0; column < 16; column++) {
            unsigned int color;

            if (!icon_color(map[row][column], icon_accents[which], &color))
                continue;
            if (map[row][column] != 'K' && row < 7)
                color = LIGHTER(color, 70 - row * 9); /* gloss, strongest at the top */
            if (scale == 1)
                plot(origin_x + x + column, origin_y + y + row, color);
            else
                gfx_fill(x + column * scale, y + row * scale, scale, scale, color);
        }
    }
}

/* ---- The wallpaper: a sky gradient with glassy bubbles, drawn once per theme -- */

static void bubble(int cx, int cy, int r)
{
    int hx = cx - r / 3, hy = cy - r / 3, hr = r / 4 + 1;

    for (int y = cy - r; y <= cy + r; y++) {
        for (int x = cx - r; x <= cx + r; x++) {
            int dx = x - cx, dy = y - cy, d = dx * dx + dy * dy, amount = 0;

            if (x < 0 || y < 0 || x >= SCREEN_W || y >= SCREEN_H || d > r * r - r) /* r*r - r: no spikes */
                continue;
            if (d > r * r - 4 * r)
                amount = 110; /* the rim catches the light */
            else
                amount = 22 + 40 * d / (r * r); /* clearer in the middle */
            if ((x - hx) * (x - hx) + (y - hy) * (y - hy) <= hr * hr)
                amount = 190; /* the highlight */
            wallpaper[y * SCREEN_W + x] = mix(wallpaper[y * SCREEN_W + x], 0xFFFFFF, amount);
        }
    }
}

void gfx_wallpaper(void)
{
    static const short bubbles[][3] = {
        {520, 70, 46}, {585, 150, 22}, {470, 160, 14}, {90, 300, 30}, {150, 250, 12},
        {360, 95, 9}, {610, 290, 34}, {300, 320, 18}, {420, 260, 11}, {240, 60, 16},
    };
    int horizon = (SCREEN_H - TASKBAR_H) * 3 / 4;

    for (int y = 0; y < SCREEN_H; y++) {
        unsigned int color = y < horizon ? mix(theme->sky_top, theme->sky_bottom, y * 256 / horizon)
                                         : mix(theme->sky_bottom, LIGHTER(theme->sky_bottom, 120),
                                               (y - horizon) * 256 / (SCREEN_H - horizon));

        for (int x = 0; x < SCREEN_W; x++)
            wallpaper[y * SCREEN_W + x] = color;
    }
    for (unsigned int i = 0; i < sizeof(bubbles) / sizeof(bubbles[0]); i++)
        bubble(bubbles[i][0], bubbles[i][1], bubbles[i][2]);
}

void gfx_draw_wallpaper(int top, int height)
{
    memcpy(frame + top * SCREEN_W, wallpaper + top * SCREEN_W, (unsigned int)(height * SCREEN_W) * 4);
}

/* ---- Pointer ------------------------------------------------------------------- */

static const char *const arrow[] = {
    "X          ", "XX         ", "X.X        ", "X..X       ", "X...X      ", "X....X     ",
    "X.....X    ", "X......X   ", "X.......X  ", "X........X ", "X.....XXXXX", "X..X..X    ",
    "X.X X..X   ", "XX  X..X   ", "X    X..X  ", "     XXXX  ", 0,
};
static const char *const resize[] = {
    "XXXXXX     ", "X....X     ", "X...X      ", "X..X.X     ", "X.X X.X    ", "XX   X.X  X",
    "      X.X.X", "       X..X", "      X...X", "     X....X", "     XXXXXX", 0,
};
static const char *const move[] = {
    "     X     ", "    X.X    ", "   X...X   ", "  XXX.XXX  ", " X  X.X  X ", "X....X....X",
    " X  X.X  X ", "  XXX.XXX  ", "   X...X   ", "    X.X    ", "     X     ", 0,
};

void gfx_pointer(int x, int y, enum pointer_kind kind)
{
    const char *const *shape = kind == POINTER_RESIZE ? resize : kind == POINTER_MOVE ? move : arrow;
    int offset = kind == POINTER_MOVE ? 5 : 0;

    gfx_unclip();
    for (int row = 0; shape[row]; row++) {
        for (int column = 0; shape[row][column]; column++) {
            if (shape[row][column] == 'X')
                plot(x + column - offset, y + row - offset, INK);
            else if (shape[row][column] == '.')
                plot(x + column - offset, y + row - offset, 0xFFFFFF);
        }
    }
}

void gfx_present(void)
{
    fb_blit(frame, SCREEN_W, SCREEN_H);
}

void copy_string(char *out, unsigned int size, const char *text)
{
    unsigned int n = strlen(text);

    if (n >= size)
        n = size - 1;
    memcpy(out, text, n);
    out[n] = 0;
}

int ends_with(const char *text, const char *suffix)
{
    unsigned int a = strlen(text), b = strlen(suffix);

    return a >= b && strcmp(text + a - b, suffix) == 0;
}
