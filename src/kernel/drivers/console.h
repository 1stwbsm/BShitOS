/* Text console: framebuffer plus COM1 for output, PS/2 plus COM1 for input. */
#ifndef KERNEL_CONSOLE_H
#define KERNEL_CONSOLE_H

#include <bshit/bootinfo.h>

void console_init(const struct bootinfo_framebuffer *fb);
void console_clear(void);
void console_write(const char *s, uint32_t length);
void console_print(const char *s);
void console_print_u32(uint32_t value);
void console_print_hex(uint32_t value);
/* Blocks for an edited line, including its '\n'. Returns the byte count. */
uint32_t console_read_line(char *buf, uint32_t size);
/* Returns a waiting key (ASCII or BS_KEY_*) without blocking, or 0. */
uint8_t console_poll_key(void);
/* Mirrors console output to COM1. Off by default. */
void console_set_serial(int enabled);
/* Lets a program own the screen; the text comes back at graphics_end. */
void console_blit(const uint32_t *pixels, uint32_t width, uint32_t height);
void console_graphics_end(void);
void console_set_colors(uint32_t fg, uint32_t bg);
/* Returns columns << 16 | rows. */
uint32_t console_size(void);
/* Copies one row of text (trailing spaces trimmed). Returns its length, or -1. */
int console_line(uint32_t row, char *out, uint32_t size);

/* Colour attributes of a row (see console.c); returns how many. */
int console_attrs(uint32_t row, uint8_t *out, uint32_t size);
/* Makes the console a terminal in x, y, w, h (pixels); w = 0 for the whole screen. */
int console_viewport(uint32_t x, uint32_t y, uint32_t w, uint32_t h);

#endif
