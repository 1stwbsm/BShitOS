#include <bshit/font5x7.h>

#include "drivers/fb.h"
#include "lib/string.h"

#define SCALE 2u
#define CELL_W (6u * SCALE)
#define CELL_H (8u * SCALE)

static struct {
    uint32_t *base;
    uint32_t width, height, stride;
    int bgr;
} fb;

void fb_init(const struct bootinfo_framebuffer *info)
{
    if (info->format == BOOTINFO_FB_NONE)
        return;
    fb.base = (uint32_t *)(uintptr_t)info->base;
    fb.width = info->width;
    fb.height = info->height;
    fb.stride = info->pixels_per_line;
    fb.bgr = info->format == BOOTINFO_FB_BGRX;
}

void fb_resolution(uint32_t *width, uint32_t *height)
{
    *width = fb.width;
    *height = fb.height;
}

void fb_size(uint32_t *cols, uint32_t *rows)
{
    *cols = fb.width / CELL_W;
    *rows = fb.height / CELL_H;
}

static uint32_t native(uint32_t c)
{
    return fb.bgr ? c : ((c & 0xFF) << 16) | (c & 0xFF00) | ((c >> 16) & 0xFF);
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
    if (fb.base)
        fill(0, 0, fb.width, fb.height, native(color));
}

/* Double lines: strokes at these offsets inside a cell, LINE pixels thick. */
#define LINE 2u
#define LEFT_STROKE 3u
#define RIGHT_STROKE 7u
#define TOP_STROKE 5u
#define BOTTOM_STROKE 9u

static void draw_shape(uint8_t shape, uint32_t x, uint32_t y, uint32_t p)
{
    const uint32_t v1 = x + LEFT_STROKE, v2 = x + RIGHT_STROKE;
    const uint32_t h1 = y + TOP_STROKE, h2 = y + BOTTOM_STROKE;

    switch (shape) {
    case FB_GLYPH_BLOCK:
        fill(x, y, CELL_W, CELL_H, p);
        break;
    case FB_GLYPH_DOUBLE_HORIZONTAL:
        fill(x, h1, CELL_W, LINE, p);
        fill(x, h2, CELL_W, LINE, p);
        break;
    case FB_GLYPH_DOUBLE_VERTICAL:
        fill(v1, y, LINE, CELL_H, p);
        fill(v2, y, LINE, CELL_H, p);
        break;
    case FB_GLYPH_DOUBLE_DOWN_RIGHT:
        fill(v1, h1, CELL_W - LEFT_STROKE, LINE, p);
        fill(v1, h1, LINE, CELL_H - TOP_STROKE, p);
        fill(v2, h2, CELL_W - RIGHT_STROKE, LINE, p);
        fill(v2, h2, LINE, CELL_H - BOTTOM_STROKE, p);
        break;
    case FB_GLYPH_DOUBLE_DOWN_LEFT:
        fill(x, h1, RIGHT_STROKE + LINE, LINE, p);
        fill(v2, h1, LINE, CELL_H - TOP_STROKE, p);
        fill(x, h2, LEFT_STROKE + LINE, LINE, p);
        fill(v1, h2, LINE, CELL_H - BOTTOM_STROKE, p);
        break;
    case FB_GLYPH_DOUBLE_UP_RIGHT:
        fill(v1, y, LINE, BOTTOM_STROKE + LINE, p);
        fill(v1, h2, CELL_W - LEFT_STROKE, LINE, p);
        fill(v2, y, LINE, TOP_STROKE + LINE, p);
        fill(v2, h1, CELL_W - RIGHT_STROKE, LINE, p);
        break;
    case FB_GLYPH_DOUBLE_UP_LEFT:
        fill(v2, y, LINE, BOTTOM_STROKE + LINE, p);
        fill(x, h2, RIGHT_STROKE + LINE, LINE, p);
        fill(v1, y, LINE, TOP_STROKE + LINE, p);
        fill(x, h1, LEFT_STROKE + LINE, LINE, p);
        break;
    default:
        break;
    }
}

void fb_put(uint32_t col, uint32_t row, char c, uint32_t fg, uint32_t bg)
{
    const uint8_t *glyph;
    uint32_t x = col * CELL_W, y = row * CELL_H;
    uint32_t fg_pixel = native(fg);

    if (!fb.base)
        return;
    fill(x, y, CELL_W, CELL_H, native(bg));
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
                fill(x + gx * SCALE, y + gy * SCALE, SCALE, SCALE, fg_pixel);
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
    uint32_t rows = fb.height / CELL_H;

    if (!fb.base)
        return;
    memmove(fb.base, fb.base + CELL_H * fb.stride, (rows - 1) * CELL_H * fb.stride * 4);
    fill(0, (rows - 1) * CELL_H, fb.width, CELL_H, native(bg));
}
