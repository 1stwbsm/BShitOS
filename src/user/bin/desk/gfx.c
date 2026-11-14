#include <bshit/font5x7.h>

#include "bs.h"
#include "desk.h"

static unsigned int frame[SCREEN_W * SCREEN_H];
static int origin_x, origin_y, clip_left, clip_top, clip_right, clip_bottom = SCREEN_H;

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

static void plot(int x, int y, unsigned int color)
{
    if (x >= clip_left && x < clip_right && y >= clip_top && y < clip_bottom)
        frame[y * SCREEN_W + x] = color;
}

void gfx_fill(int x, int y, int w, int h, unsigned int color)
{
    int left = x + origin_x, top = y + origin_y, right = left + w, bottom = top + h;

    if (left < clip_left)
        left = clip_left;
    if (top < clip_top)
        top = clip_top;
    if (right > clip_right)
        right = clip_right;
    if (bottom > clip_bottom)
        bottom = clip_bottom;
    for (int row = top; row < bottom; row++)
        for (int column = left; column < right; column++)
            frame[row * SCREEN_W + column] = color;
}

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

void gfx_text_scaled(int x, int y, const char *s, int scale, unsigned int color)
{
    for (; *s; s++, x += CHAR_W * scale)
        glyph(x, y, *s, scale, color);
}

void gfx_outline(int x, int y, int w, int h, unsigned int color)
{
    gfx_fill(x, y, w, 1, color);
    gfx_fill(x, y + h - 1, w, 1, color);
    gfx_fill(x, y, 1, h, color);
    gfx_fill(x + w - 1, y, 1, h, color);
}

void gfx_button(int x, int y, int w, int h, const char *label, int highlighted)
{
    gfx_fill(x, y, w, h, highlighted ? theme->select : theme->field);
    gfx_outline(x, y, w, h, theme->line);
    gfx_text(x + (w - (int)strlen(label) * CHAR_W) / 2, y + (h - 7) / 2, label, theme->text);
}

void gfx_bar(int x, int y, int w, int h, unsigned int used, unsigned int total, unsigned int color)
{
    int filled;

    /* Keep the product inside 32 bits; there is no 64-bit division here. */
    while (total > (1u << 20)) {
        total >>= 1;
        used >>= 1;
    }
    filled = total ? (int)(used * (unsigned int)(w - 2) / total) : 0;

    gfx_fill(x, y, w, h, theme->field);
    gfx_outline(x, y, w, h, theme->line);
    gfx_fill(x + 1, y + 1, filled, h - 2, color);
}

int gfx_inside(int px, int py, int x, int y, int w, int h)
{
    return px >= x && py >= y && px < x + w && py < y + h;
}

void gfx_pointer(int x, int y, enum pointer_kind kind)
{
    const char *const *shape = kind == POINTER_RESIZE ? resize : kind == POINTER_MOVE ? move : arrow;
    int offset = kind == POINTER_MOVE ? 5 : 0;

    gfx_unclip();
    for (int row = 0; shape[row]; row++) {
        for (int column = 0; shape[row][column]; column++) {
            if (shape[row][column] == 'X')
                plot(x + column - offset, y + row - offset, 0x000000);
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
