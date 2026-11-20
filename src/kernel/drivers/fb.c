/*
 * The screen behind the console: a linear framebuffer, where text is drawn from
 * the 5x7 font (scaled up on big screens), or VGA text mode, where the card
 * draws the characters itself. Text mode is the lowest common denominator: it
 * works on anything with a VGA card, and graphics programs simply get no screen.
 */
#include <bshit/font5x7.h>

#include "arch/x86/cpu.h"
#include "drivers/fb.h"
#include "lib/string.h"

static struct {
    uint32_t *base;
    uint16_t *text; /* VGA text mode: character + attribute per cell */
    uint32_t width, height, stride; /* pixels, or cells in text mode */
    uint32_t scale, cell_w, cell_h;
    int bgr;
} fb;

/* The 16 VGA text colours, in attribute order. */
static const uint32_t vga_palette[16] = {
    0x000000, 0x0000AA, 0x00AA00, 0x00AAAA, 0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
    0x555555, 0x5555FF, 0x55FF55, 0x55FFFF, 0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF,
};

/* fb_glyph shapes in code page 437, which every VGA card has in ROM. */
static const uint8_t cp437_shapes[] = {0xDB, 0xCD, 0xBA, 0xC9, 0xBB, 0xC8, 0xBC};

static void text_mode_setup(void)
{
    outb(0x3D4, 0x0A); /* hide the hardware cursor: the console draws its own */
    outb(0x3D5, 0x20);
    inb(0x3DA); /* the attribute controller: bright backgrounds instead of blinking */
    outb(0x3C0, 0x30);
    outb(0x3C0, (uint8_t)(inb(0x3C1) & ~0x08));
    outb(0x3C0, 0x20); /* give the palette back to the display */
}

void fb_init(const struct bootinfo_framebuffer *info)
{
    if (info->format == BOOTINFO_FB_NONE)
        return;
    if (info->format == BOOTINFO_FB_TEXT) {
        fb.text = (uint16_t *)(uintptr_t)info->base;
        fb.width = info->width;
        fb.height = info->height;
        fb.stride = info->pixels_per_line;
        text_mode_setup();
        return;
    }
    fb.base = (uint32_t *)(uintptr_t)info->base;
    fb.width = info->width;
    fb.height = info->height;
    fb.stride = info->pixels_per_line;
    fb.bgr = info->format == BOOTINFO_FB_BGRX;
    fb.scale = fb.width >= 1024 ? 2 : 1; /* 640x480 gets small text, not 53 columns */
    fb.cell_w = 6 * fb.scale;
    fb.cell_h = 8 * fb.scale;
}

/* Pixels; 0x0 in text mode, which tells graphics programs there is no screen. */
void fb_resolution(uint32_t *width, uint32_t *height)
{
    *width = fb.base ? fb.width : 0;
    *height = fb.base ? fb.height : 0;
}

void fb_size(uint32_t *cols, uint32_t *rows)
{
    if (fb.text) {
        *cols = fb.width;
        *rows = fb.height;
        return;
    }
    *cols = fb.base ? fb.width / fb.cell_w : 0;
    *rows = fb.base ? fb.height / fb.cell_h : 0;
}

static uint32_t native(uint32_t c)
{
    return fb.bgr ? c : ((c & 0xFF) << 16) | (c & 0xFF00) | ((c >> 16) & 0xFF);
}

/* The nearest of the 16 text colours. */
static uint8_t vga_color(uint32_t c)
{
    uint32_t best = 0, best_distance = ~0u;

    for (uint32_t i = 0; i < 16; i++) {
        int dr = (int)((c >> 16) & 255) - (int)((vga_palette[i] >> 16) & 255);
        int dg = (int)((c >> 8) & 255) - (int)((vga_palette[i] >> 8) & 255);
        int db = (int)(c & 255) - (int)(vga_palette[i] & 255);
        uint32_t distance = (uint32_t)(dr * dr + dg * dg + db * db);

        if (distance < best_distance) {
            best_distance = distance;
            best = i;
        }
    }
    return (uint8_t)best;
}

static uint16_t text_cell(char c, uint32_t fg, uint32_t bg)
{
    uint8_t ch = (uint8_t)c;

    if (ch >= FB_GLYPH_BLOCK && ch < FB_GLYPH_BLOCK + sizeof(cp437_shapes))
        ch = cp437_shapes[ch - FB_GLYPH_BLOCK];
    else if (ch < 0x20 || ch > 0x7E)
        ch = ' ';
    return (uint16_t)(ch | (vga_color(fg) | vga_color(bg) << 4) << 8);
}

static void fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t pixel)
{
    for (uint32_t row = y; row < y + h && row < fb.height; row++) {
        uint32_t *line = fb.base + row * fb.stride;

        for (uint32_t col = x; col < x + w && col < fb.width; col++)
            line[col] = pixel;
    }
}

void fb_clear(uint32_t color)
{
    if (fb.text) {
        for (uint32_t i = 0; i < fb.stride * fb.height; i++)
            fb.text[i] = text_cell(' ', color, color);
    } else if (fb.base) {
        fill(0, 0, fb.width, fb.height, native(color));
    }
}

/* Box-drawing shapes, as strokes LINE thick at fixed fractions of the cell. */
static void draw_shape(uint8_t shape, uint32_t x, uint32_t y, uint32_t p)
{
    const uint32_t s = fb.scale, line = s, cw = fb.cell_w, ch = fb.cell_h;
    const uint32_t v1 = x + 3 * s / 2, v2 = x + 7 * s / 2, h1 = y + 5 * s / 2, h2 = y + 9 * s / 2;
    const uint32_t left = 3 * s / 2, right = 7 * s / 2, top = 5 * s / 2, bottom = 9 * s / 2;

    switch (shape) {
    case FB_GLYPH_BLOCK:
        fill(x, y, cw, ch, p);
        break;
    case FB_GLYPH_DOUBLE_HORIZONTAL:
        fill(x, h1, cw, line, p);
        fill(x, h2, cw, line, p);
        break;
    case FB_GLYPH_DOUBLE_VERTICAL:
        fill(v1, y, line, ch, p);
        fill(v2, y, line, ch, p);
        break;
    case FB_GLYPH_DOUBLE_DOWN_RIGHT:
        fill(v1, h1, cw - left, line, p);
        fill(v1, h1, line, ch - top, p);
        fill(v2, h2, cw - right, line, p);
        fill(v2, h2, line, ch - bottom, p);
        break;
    case FB_GLYPH_DOUBLE_DOWN_LEFT:
        fill(x, h1, right + line, line, p);
        fill(v2, h1, line, ch - top, p);
        fill(x, h2, left + line, line, p);
        fill(v1, h2, line, ch - bottom, p);
        break;
    case FB_GLYPH_DOUBLE_UP_RIGHT:
        fill(v1, y, line, bottom + line, p);
        fill(v1, h2, cw - left, line, p);
        fill(v2, y, line, top + line, p);
        fill(v2, h1, cw - right, line, p);
        break;
    case FB_GLYPH_DOUBLE_UP_LEFT:
        fill(v2, y, line, bottom + line, p);
        fill(x, h2, right + line, line, p);
        fill(v1, y, line, top + line, p);
        fill(x, h1, left + line, line, p);
        break;
    default:
        break;
    }
}

void fb_put(uint32_t col, uint32_t row, char c, uint32_t fg, uint32_t bg)
{
    const uint8_t *glyph;
    uint32_t x = col * fb.cell_w, y = row * fb.cell_h, fg_pixel = native(fg);

    if (fb.text) {
        if (col < fb.width && row < fb.height)
            fb.text[row * fb.stride + col] = text_cell(c, fg, bg);
        return;
    }
    if (!fb.base)
        return;
    fill(x, y, fb.cell_w, fb.cell_h, native(bg));
    if ((uint8_t)c >= FB_GLYPH_BLOCK) {
        draw_shape((uint8_t)c, x, y, fg_pixel);
        return;
    }
    if (c <= 0x20 || c > 0x7E)
        return;
    glyph = bs_font5x7[c - 0x20];
    for (uint32_t gx = 0; gx < 5; gx++)
        for (uint32_t gy = 0; gy < 7; gy++)
            if (glyph[gx] & (1u << gy))
                fill(x + gx * fb.scale, y + gy * fb.scale, fb.scale, fb.scale, fg_pixel);
}

void fb_blit(const uint32_t *pixels, uint32_t width, uint32_t height)
{
    uint32_t scale, left, top;

    if (!fb.base || width == 0 || height == 0 || width > fb.width || height > fb.height)
        return;
    scale = fb.width / width < fb.height / height ? fb.width / width : fb.height / height;
    left = (fb.width - width * scale) / 2;
    top = (fb.height - height * scale) / 2;

    for (uint32_t y = 0; y < height; y++) {
        uint32_t *first = fb.base + (top + y * scale) * fb.stride + left;
        uint32_t *out = first;

        for (uint32_t x = 0; x < width; x++) {
            uint32_t pixel = native(pixels[y * width + x]);

            for (uint32_t s = 0; s < scale; s++)
                *out++ = pixel;
        }
        for (uint32_t s = 1; s < scale; s++)
            __builtin_memcpy(first + s * fb.stride, first, width * scale * 4);
    }
}

void fb_scroll(uint32_t bg)
{
    if (fb.text) {
        memmove(fb.text, fb.text + fb.stride, (fb.height - 1) * fb.stride * 2);
        for (uint32_t c = 0; c < fb.width; c++)
            fb.text[(fb.height - 1) * fb.stride + c] = text_cell(' ', bg, bg);
        return;
    }
    if (!fb.base)
        return;
    memmove(fb.base, fb.base + fb.cell_h * fb.stride, (fb.height / fb.cell_h - 1) * fb.cell_h * fb.stride * 4);
    fill(0, (fb.height / fb.cell_h - 1) * fb.cell_h, fb.width, fb.cell_h, native(bg));
}
