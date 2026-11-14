#ifndef KERNEL_FB_H
#define KERNEL_FB_H

#include <bshit/bootinfo.h>

/* A text grid over the linear framebuffer. Colours are 0xRRGGBB. */
/* Cell values above ASCII: shapes drawn to fill the whole cell. */
enum fb_glyph {
    FB_GLYPH_BLOCK = 0x80,
    FB_GLYPH_DOUBLE_HORIZONTAL,
    FB_GLYPH_DOUBLE_VERTICAL,
    FB_GLYPH_DOUBLE_DOWN_RIGHT,
    FB_GLYPH_DOUBLE_DOWN_LEFT,
    FB_GLYPH_DOUBLE_UP_RIGHT,
    FB_GLYPH_DOUBLE_UP_LEFT,
};

void fb_init(const struct bootinfo_framebuffer *fb);
void fb_size(uint32_t *cols, uint32_t *rows);
void fb_resolution(uint32_t *width, uint32_t *height);
void fb_clear(uint32_t color);
void fb_put(uint32_t col, uint32_t row, char c, uint32_t fg, uint32_t bg);
void fb_scroll(uint32_t bg);
/* Draws 0xRRGGBB pixels centred at the largest integer scale that fits. */
void fb_blit(const uint32_t *pixels, uint32_t width, uint32_t height);

#endif
