/*
 * Drawing. Everything lands in a frame of screen_w x screen_h layout pixels
 * that the kernel scales onto the screen. Skins decide what things look like;
 * these are the primitives they use.
 */
#include <bshit/font5x7.h>

#include "bdeskui.h"

/* Sized for the biggest layout; pages beyond the one in use are never touched,
 * so they take no memory. */
static unsigned int frame[MAX_W * MAX_H];
int screen_w = MIN_W, screen_h = MIN_H;
static int origin_x, origin_y, clip_left, clip_top, clip_right = MIN_W, clip_bottom = MIN_H;
static int real_w, real_h; /* the screen in real pixels */

void gfx_init(int width, int height)
{
    struct bs_sysinfo info;

    sysinfo(&info);
    real_w = (int)info.screen_width;
    real_h = (int)info.screen_height;
    screen_w = width;
    screen_h = height;
    gfx_unclip();
}

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

void gfx_origin(int *x, int *y)
{
    *x = origin_x;
    *y = origin_y;
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

void gfx_background(void)
{
    gfx_unclip();
    gfx_fill(0, 0, SCREEN_W, SCREEN_H, skin->background);
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

void gfx_chrome(int x, int y, int w, int h, unsigned int base, int pressed)
{
    int upper = h * 9 / 20, g = skin->gloss;

    if (g == 0) { /* flat skins */
        gfx_fill(x, y, w, h, pressed ? DARKER(base, 40) : base);
        return;
    }
    if (pressed) {
        gfx_gradient(x, y, w, h, DARKER(base, 60), LIGHTER(base, g / 3));
        return;
    }
    gfx_gradient(x, y, w, upper, LIGHTER(base, g), LIGHTER(base, g / 3));
    gfx_gradient(x, y + upper, w, h - upper, DARKER(base, 12), LIGHTER(base, g / 5));
    gfx_fill(x + 1, y + 1, w - 2, 1, LIGHTER(base, g + 40 > 256 ? 256 : g + 40)); /* the specular line */
}

void gfx_inset(int x, int y, int w, int h, unsigned int fill)
{
    gfx_fill(x, y, w, h, fill);
    if (skin->gloss) {
        gfx_fill(x, y, w, 2, DARKER(fill, 60));
        gfx_fill(x, y, 2, h, DARKER(fill, 40));
    }
    gfx_outline(x - 1, y - 1, w + 2, h + 2, skin->line);
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
    gfx_outline(x, y, w, h, skin->line);
    gfx_outline(x + 1, y + 1, w - 2, h - 2, skin->line);
}

/* Bresenham, stamped thickness x thickness. */
void gfx_line(int x0, int y0, int x1, int y1, int thickness, unsigned int color)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y0 - y1 : y1 - y0;
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, error = dx + dy;

    for (;;) {
        gfx_fill(x0, y0, thickness, thickness, color);
        if (x0 == x1 && y0 == y1)
            return;
        if (2 * error >= dy) {
            error += dy;
            x0 += sx;
        }
        if (2 * error <= dx) {
            error += dx;
            y0 += sy;
        }
    }
}

void gfx_disc(int cx, int cy, int r, unsigned int color)
{
    for (int y = -r; y <= r; y++)
        for (int x = -r; x <= r; x++)
            if (x * x + y * y <= r * r - r / 2)
                gfx_fill(cx + x, cy + y, 1, 1, color);
}

void gfx_darken(int x, int y, int w, int h, int amount)
{
    int left = x + origin_x, top = y + origin_y, right = left + w, bottom = top + h;

    if (!span(&left, &top, &right, &bottom))
        return;
    for (int row = top; row < bottom; row++)
        for (int column = left; column < right; column++)
            frame[row * SCREEN_W + column] = mix(frame[row * SCREEN_W + column], 0x000000, amount);
}

void gfx_shadow(int x, int y, int w, int h, int offset, int amount)
{
    if (offset <= 0)
        return;
    gfx_darken(x + offset, y + h, w, offset, amount);
    gfx_darken(x + w, y + offset, offset, h - offset, amount);
}

void gfx_slab(int x, int y, int w, int h, unsigned int base, int pressed)
{
    unsigned int line = skin->line;

    gfx_gel(x + 1, y + 1, w - 2, h - 2, base, pressed);
    /* The frame skips the corner pixels, and the cut closes diagonally inside. */
    gfx_fill(x + 1, y, w - 2, 1, line);
    gfx_fill(x + 1, y + h - 1, w - 2, 1, line);
    gfx_fill(x, y + 1, 1, h - 2, line);
    gfx_fill(x + w - 1, y + 1, 1, h - 2, line);
    plot(origin_x + x + 1, origin_y + y + 1, line);
    plot(origin_x + x + w - 2, origin_y + y + 1, line);
    plot(origin_x + x + 1, origin_y + y + h - 2, line);
    plot(origin_x + x + w - 2, origin_y + y + h - 2, line);
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

/* ---- Controls: the skin's ------------------------------------------------------ */

void gfx_button(int x, int y, int w, int h, const char *label, int highlighted)
{
    skin->draw_button(x, y, w, h, label, highlighted);
}

/* A tube filling up. */
void gfx_bar(int x, int y, int w, int h, unsigned int used, unsigned int total, unsigned int color)
{
    int filled;

    while (total > (1u << 20)) { /* keep the product inside 32 bits */
        total >>= 1;
        used >>= 1;
    }
    filled = total ? (int)(used * (unsigned int)(w - 2) / total) : 0;
    gfx_inset(x, y, w, h, skin->field);
    if (filled > 0) {
        if (skin->gloss)
            gfx_gel(x + 1, y + 1, filled, h - 2, color, 0);
        else
            gfx_fill(x + 1, y + 1, filled, h - 2, color);
    }
}

int gfx_inside(int px, int py, int x, int y, int w, int h)
{
    return px >= x && py >= y && px < x + w && py < y + h;
}

/* ---- Icons: 16x16 maps, drawn in the skin's icon style -------------------------- */

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
    [ICON_USER] = {
        "................", "......KKKK......", ".....KSSSSK.....", "....KSSSSSSK....",
        "....KSSSSSSK....", "....KSSSSSSK....", ".....KSSSSK.....", "......KKKK......",
        "....KKBBBBKK....", "...KBBBBBBBBK...", "..KBBBBBBBBBBK..", "..KBBBBBBBBBBK..",
        "..KBBBBBBBBBBK..", "..KKKKKKKKKKKK..", "................", "................",
    },
};

static const unsigned int icon_accents[ICON_COUNT] = {
    [ICON_CUBE] = 0x8E5BD8, [ICON_PROGRAM] = 0x2F86D8,
};

static const unsigned int vga[16] = {
    0x000000, 0x0000AA, 0x00AA00, 0x00AAAA, 0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
    0x555555, 0x5555FF, 0x55FF55, 0x55FFFF, 0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF,
};

static unsigned int nearest_vga(unsigned int c)
{
    unsigned int best = 0, best_distance = ~0u;

    for (int i = 0; i < 16; i++) {
        int dr = (int)((c >> 16) & 255) - (int)((vga[i] >> 16) & 255);
        int dg = (int)((c >> 8) & 255) - (int)((vga[i] >> 8) & 255);
        int db = (int)(c & 255) - (int)(vga[i] & 255);
        unsigned int d = (unsigned int)(dr * dr + dg * dg + db * db);

        if (d < best_distance) {
            best_distance = d;
            best = vga[i];
        }
    }
    return best;
}

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

/* Recolours one icon pixel for the skin's style; 0 leaves it out. */
static int styled(char c, int row, unsigned int color, unsigned int *out)
{
    switch (skin->icons) {
    case ICONS_LINE: /* light lines; fills only as a faint tint, warm colours as the accent */
        if (c == 'K')
            *out = skin->chrome_text;
        else if (c == 'W' || c == 'w' || c == 'S')
            return 0;
        else if (c == 'Y' || c == 'O' || c == 'R')
            *out = mix(skin->accent, skin->background, 60);
        else
            *out = mix(color, skin->background, 150);
        return 1;
    case ICONS_EGA:
        *out = nearest_vga(color);
        return 1;
    case ICONS_PICTO: /* black, white, and the accent for anything warm */
        if (c == 'W' || c == 'w' || c == 'S')
            *out = 0xFFFFFF;
        else if (c == 'Y' || c == 'O' || c == 'R' || c == 'A' || c == 'L')
            *out = skin->accent;
        else if (c == 'g')
            *out = 0x8A8A8A;
        else
            *out = 0x111111;
        return 1;
    default: /* glossy: a sheen, strongest at the top */
        *out = c != 'K' && row < 7 ? LIGHTER(color, 70 - row * 9) : color;
        return 1;
    }
}

void gfx_icon(int x, int y, enum icon which, int scale)
{
    const char *const *map = icon_maps[which];

    for (int row = 0; row < 16; row++) {
        for (int column = 0; column < 16; column++) {
            unsigned int color;

            if (!icon_color(map[row][column], icon_accents[which], &color) ||
                !styled(map[row][column], row, color, &color))
                continue;
            if (scale == 1)
                plot(origin_x + x + column, origin_y + y + row, color);
            else
                gfx_fill(x + column * scale, y + row * scale, scale, scale, color);
        }
    }
}

/* ---- The pointer, in the skin's style ---------------------------------------- */

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
    unsigned int edge = INK, fill = 0xFFFFFF;

    gfx_unclip();
    if (skin->pointer == POINTER_BLOCK && kind == POINTER_ARROW) { /* a text-mode mouse cell */
        gfx_fill(x, y, CHAR_W + 1, CHAR_H + 2, INK);
        gfx_fill(x + 1, y + 1, CHAR_W - 1, CHAR_H, skin->accent);
        return;
    }
    if (skin->pointer == POINTER_HEAVY)
        fill = skin->accent;
    for (int row = 0; shape[row]; row++) {
        for (int column = 0; shape[row][column]; column++) {
            char c = shape[row][column];

            if (c == 'X') {
                plot(x + column - offset, y + row - offset, edge);
                if (skin->pointer == POINTER_HEAVY) /* a doubled edge */
                    plot(x + column - offset + 1, y + row - offset, edge);
            } else if (c == '.') {
                plot(x + column - offset, y + row - offset, fill);
            }
        }
    }
}

void gfx_present(void)
{
    fb_blit(frame, (unsigned int)SCREEN_W, (unsigned int)SCREEN_H);
}

/* As the kernel scales the frame: the biggest whole scale that fits, centred. */
void gfx_screen_rect(int x, int y, int w, int h, int *sx, int *sy, int *sw, int *sh)
{
    int scale = real_w / SCREEN_W < real_h / SCREEN_H ? real_w / SCREEN_W : real_h / SCREEN_H;
    int left = (real_w - SCREEN_W * scale) / 2, top = (real_h - SCREEN_H * scale) / 2;

    if (scale < 1)
        scale = 1;
    *sx = left + x * scale;
    *sy = top + y * scale;
    *sw = w * scale;
    *sh = h * scale;
}

/* The kernel draws console text 2x on screens 1024 or wider; BDeskUI's text is 1x layout. */
int gfx_text_scale(void)
{
    int scale = real_w / SCREEN_W < real_h / SCREEN_H ? real_w / SCREEN_W : real_h / SCREEN_H;
    int console = real_w >= 1024 ? 2 : 1;

    return scale >= console ? 1 : console / (scale < 1 ? 1 : scale);
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
