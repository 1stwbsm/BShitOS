#include <bshit/syscall.h>

#include "drivers/acpi.h"
#include "drivers/console.h"
#include "drivers/fb.h"
#include "drivers/ps2.h"
#include "drivers/serial.h"
#include "lib/string.h"
#include "net/net.h"

#define MAX_COLS 320
#define MAX_ROWS 120
#define HISTORY_LINES 16
#define HISTORY_WIDTH 256

/* A copy of the text, so it can be restored after a graphics program. */
static char cells[MAX_ROWS][MAX_COLS];
static uint8_t attrs[MAX_ROWS][MAX_COLS];
static uint32_t fg_color = 0xD8D8D8, bg_color = 0x000000;
static uint8_t current_attr;
static uint32_t cols, rows, col, row;
static uint32_t saved_col, saved_row; /* ESC[s / ESC[u */
static int serial_enabled;
static int graphics_active;
static uint32_t utf8_codepoint, utf8_remaining;

#define ESCAPE_PARAMS 4u
enum { ESCAPE_NONE, ESCAPE_STARTED, ESCAPE_PARAMS_STATE };
static int escape_state;
static uint32_t escape_params[ESCAPE_PARAMS], escape_count;

static char history[HISTORY_LINES][HISTORY_WIDTH];
static uint32_t history_count;

/* attrs: bits 0-4 colour (0 = default, 1-16 = palette), bit 5 inverse, bit 6 bold. */
#define ATTR_COLOR 0x1Fu
#define ATTR_INVERSE 0x20u
#define ATTR_BOLD 0x40u

static const uint32_t palette[16] = {
    0x4A4A4A, 0xE0564A, 0x7BC86C, 0xE8C547, 0x5C8DEB, 0xC678DD, 0x56C2D6, 0xD8D8D8,
    0x8A8A8A, 0xFF7A6E, 0x9EE891, 0xFFE070, 0x82AEFF, 0xE3A0F5, 0x84E3F0, 0xFFFFFF,
};

static uint32_t foreground_of(uint8_t attr)
{
    uint32_t index = attr & ATTR_COLOR;

    if (index == 0)
        return fg_color;
    if ((attr & ATTR_BOLD) && index <= 8)
        index += 8;
    return palette[index - 1];
}

static void draw_cell(uint32_t c, uint32_t r)
{
    uint32_t fg;

    if (graphics_active)
        return;
    fg = foreground_of(attrs[r][c]);
    if (attrs[r][c] & ATTR_INVERSE)
        fb_put(c, r, cells[r][c], bg_color, fg);
    else
        fb_put(c, r, cells[r][c], fg, bg_color);
}

static void draw_cursor(int visible)
{
    if (graphics_active)
        return;
    if (visible && (attrs[row][col] & ATTR_INVERSE))
        fb_put(col, row, cells[row][col], foreground_of(attrs[row][col]), bg_color);
    else if (visible)
        fb_put(col, row, cells[row][col], bg_color, foreground_of(attrs[row][col]));
    else
        draw_cell(col, row);
}

static void newline(void)
{
    col = 0;
    if (row + 1 < rows) {
        row++;
        return;
    }
    if (saved_row > 0)
        saved_row--; /* the saved spot scrolls with the text */
    memmove(cells[0], cells[1], (rows - 1) * MAX_COLS);
    memset(cells[rows - 1], ' ', MAX_COLS);
    memmove(attrs[0], attrs[1], (rows - 1) * MAX_COLS);
    memset(attrs[rows - 1], 0, MAX_COLS);
    if (!graphics_active)
        fb_scroll(bg_color);
}

static void put(char c)
{
    if (c == '\n') {
        newline();
    } else if (c == '\b') {
        if (col > 0)
            col--;
        cells[row][col] = ' ';
        attrs[row][col] = 0;
        draw_cell(col, row);
    } else {
        cells[row][col] = c;
        attrs[row][col] = current_attr;
        draw_cell(col, row);
        if (++col >= cols)
            newline();
    }
}

void console_init(const struct bootinfo_framebuffer *fb)
{
    serial_init();
    fb_init(fb);
    fb_size(&cols, &rows);
    if (cols > MAX_COLS)
        cols = MAX_COLS;
    if (rows > MAX_ROWS)
        rows = MAX_ROWS;
    ps2_init();
    console_clear();
}

void console_clear(void)
{
    memset(cells, ' ', sizeof(cells));
    memset(attrs, 0, sizeof(attrs));
    col = row = 0;
    if (!graphics_active)
        fb_clear(bg_color);
    draw_cursor(1);
}

void console_set_serial(int enabled)
{
    serial_enabled = enabled;
}

static char glyph_for(uint32_t codepoint)
{
    switch (codepoint) {
    case 0x2588: return (char)FB_GLYPH_BLOCK;
    case 0x2550: return (char)FB_GLYPH_DOUBLE_HORIZONTAL;
    case 0x2551: return (char)FB_GLYPH_DOUBLE_VERTICAL;
    case 0x2554: return (char)FB_GLYPH_DOUBLE_DOWN_RIGHT;
    case 0x2557: return (char)FB_GLYPH_DOUBLE_DOWN_LEFT;
    case 0x255A: return (char)FB_GLYPH_DOUBLE_UP_RIGHT;
    case 0x255D: return (char)FB_GLYPH_DOUBLE_UP_LEFT;
    default:     return '?';
    }
}

/* Blank cells are already drawn blank, so erasing only touches the rest. */
static void erase_cell(uint32_t c, uint32_t r)
{
    if (cells[r][c] == ' ' && attrs[r][c] == 0)
        return;
    cells[r][c] = ' ';
    attrs[r][c] = 0;
    draw_cell(c, r);
}

static void erase_to_line_end(void)
{
    for (uint32_t c = col; c < cols; c++)
        erase_cell(c, row);
}

/* The ANSI subset programs use: CUP, CUB, CUF, ED, EL, save/restore and SGR colours, bold, inverse. */
static void run_escape(char final)
{
    switch (final) {
    case 'H':
        row = escape_params[0] ? escape_params[0] - 1 : 0;
        col = escape_params[1] ? escape_params[1] - 1 : 0;
        if (row >= rows)
            row = rows - 1;
        if (col >= cols)
            col = cols - 1;
        break;
    case 'J':
        if (escape_params[0] == 2) {
            console_clear();
        } else { /* to the end of the screen */
            erase_to_line_end();
            for (uint32_t r = row + 1; r < rows; r++)
                for (uint32_t c = 0; c < cols; c++)
                    erase_cell(c, r);
        }
        break;
    case 'D': /* back, wrapping to the previous line */
        for (uint32_t n = escape_params[0] ? escape_params[0] : 1; n && (col || row); n--) {
            if (col) {
                col--;
            } else {
                row--;
                col = cols - 1;
            }
        }
        break;
    case 'C': /* forward, wrapping to the next line */
        for (uint32_t n = escape_params[0] ? escape_params[0] : 1; n; n--) {
            if (col + 1 < cols)
                col++;
            else if (row + 1 < rows)
                row++, col = 0;
        }
        break;
    case 's':
        saved_col = col;
        saved_row = row;
        break;
    case 'u':
        col = saved_col < cols ? saved_col : cols - 1;
        row = saved_row < rows ? saved_row : rows - 1;
        break;
    case 'K':
        erase_to_line_end();
        break;
    case 'm':
        for (uint32_t i = 0; i <= escape_count && i < ESCAPE_PARAMS; i++) {
            uint32_t p = escape_params[i];

            if (p == 0)
                current_attr = 0;
            else if (p == 1)
                current_attr |= ATTR_BOLD;
            else if (p == 22)
                current_attr &= (uint8_t)~ATTR_BOLD;
            else if (p == 7)
                current_attr |= ATTR_INVERSE;
            else if (p == 27)
                current_attr &= (uint8_t)~ATTR_INVERSE;
            else if (p >= 30 && p <= 37)
                current_attr = (uint8_t)((current_attr & ~ATTR_COLOR) | (p - 30 + 1));
            else if (p >= 90 && p <= 97)
                current_attr = (uint8_t)((current_attr & ~ATTR_COLOR) | (p - 90 + 9));
            else if (p == 39)
                current_attr &= (uint8_t)~ATTR_COLOR;
        }
        break;
    default:
        break;
    }
}

/* Returns nonzero if the byte belonged to an escape sequence. */
static int feed_escape(uint8_t byte)
{
    if (escape_state == ESCAPE_NONE) {
        if (byte != 0x1B)
            return 0;
        escape_state = ESCAPE_STARTED;
        return 1;
    }
    if (escape_state == ESCAPE_STARTED) {
        if (byte == '[') {
            escape_state = ESCAPE_PARAMS_STATE;
            escape_count = 0;
            memset(escape_params, 0, sizeof(escape_params));
        } else {
            escape_state = ESCAPE_NONE;
        }
        return 1;
    }
    if (byte >= '0' && byte <= '9') {
        if (escape_count < ESCAPE_PARAMS)
            escape_params[escape_count] = escape_params[escape_count] * 10 + (byte - '0');
    } else if (byte == ';') {
        escape_count++;
    } else {
        run_escape((char)byte);
        escape_state = ESCAPE_NONE;
    }
    return 1;
}

/* Decodes UTF-8. State persists, as a character can span two writes. */
static void put_byte(uint8_t byte)
{
    if (feed_escape(byte))
        return;
    if (byte < 0x80) {
        utf8_remaining = 0;
        put((char)byte);
    } else if ((byte & 0xC0) == 0x80) {
        if (utf8_remaining == 0)
            return;
        utf8_codepoint = utf8_codepoint << 6 | (byte & 0x3F);
        if (--utf8_remaining == 0)
            put(glyph_for(utf8_codepoint));
    } else if ((byte & 0xE0) == 0xC0) {
        utf8_codepoint = byte & 0x1F;
        utf8_remaining = 1;
    } else if ((byte & 0xF0) == 0xE0) {
        utf8_codepoint = byte & 0x0F;
        utf8_remaining = 2;
    } else if ((byte & 0xF8) == 0xF0) {
        utf8_codepoint = byte & 0x07;
        utf8_remaining = 3;
    } else {
        utf8_remaining = 0;
    }
}

void console_write(const char *s, uint32_t length)
{
    draw_cursor(0);
    for (uint32_t i = 0; i < length; i++) {
        put_byte((uint8_t)s[i]);
        if (serial_enabled)
            serial_put(s[i]);
    }
    draw_cursor(1);
}

int console_line(uint32_t r, char *out, uint32_t size)
{
    uint32_t length = cols;

    if (r >= rows || size == 0)
        return -1;
    while (length > 0 && cells[r][length - 1] == ' ')
        length--;
    if (length >= size)
        length = size - 1;
    for (uint32_t c = 0; c < length; c++) {
        char ch = cells[r][c];

        out[c] = (unsigned char)ch >= 0x80 ? '#' : ch;
    }
    out[length] = 0;
    return (int)length;
}

uint32_t console_size(void)
{
    return cols << 16 | rows;
}

static void repaint(void)
{
    if (graphics_active)
        return;
    fb_clear(bg_color);
    for (uint32_t r = 0; r < rows; r++)
        for (uint32_t c = 0; c < cols; c++)
            if (cells[r][c] != ' ' || attrs[r][c])
                draw_cell(c, r);
    draw_cursor(1);
}

void console_set_colors(uint32_t fg, uint32_t bg)
{
    fg_color = fg;
    bg_color = bg;
    repaint();
}

void console_print(const char *s)
{
    console_write(s, strlen(s));
}

void console_print_u32(uint32_t value)
{
    char buf[11];
    int i = 10;

    buf[i] = 0;
    do {
        buf[--i] = (char)('0' + value % 10);
        value /= 10;
    } while (value);
    console_print(&buf[i]);
}

void console_print_hex(uint32_t value)
{
    char buf[9];

    for (int i = 7; i >= 0; i--, value >>= 4)
        buf[i] = "0123456789ABCDEF"[value & 0xF];
    buf[8] = 0;
    console_print(buf);
}

void console_blit(const uint32_t *pixels, uint32_t width, uint32_t height)
{
    if (!graphics_active) {
        graphics_active = 1;
        fb_clear(bg_color);
    }
    fb_blit(pixels, width, height);
}

void console_graphics_end(void)
{
    if (!graphics_active)
        return;
    graphics_active = 0;
    repaint();
}

uint8_t console_poll_key(void)
{
    uint8_t c;

    acpi_poll();
    net_poll();
    c = ps2_key_poll();

    if (!c)
        c = (uint8_t)serial_poll();
    if (c == '\r')
        c = '\n';
    if (c == 0x7F)
        c = '\b';
    return c;
}

static uint8_t next_key(void)
{
    for (;;) {
        uint8_t c = console_poll_key();

        if (c)
            return c;
        __asm__ volatile("pause");
    }
}

/* ---- Line editor ------------------------------------------------------ */

static void cursor_left(uint32_t count)
{
    while (count--) {
        draw_cursor(0);
        if (col > 0) {
            col--;
        } else if (row > 0) {
            row--;
            col = cols - 1;
        }
        draw_cursor(1);
        if (serial_enabled)
            serial_put('\b');
    }
}

/* Reprints buf[from..length], blanks `erase` old cells, restores the cursor. */
static void redraw_tail(const char *buf, uint32_t from, uint32_t length, uint32_t erase)
{
    console_write(buf + from, length - from);
    for (uint32_t i = 0; i < erase; i++)
        console_write(" ", 1);
    cursor_left(length - from + erase);
}

static void replace_line(char *buf, uint32_t size, uint32_t *length, uint32_t *position, const char *text)
{
    uint32_t old_length = *length, new_length = strlen(text);

    if (new_length >= size)
        new_length = size - 1;
    cursor_left(*position);
    memcpy(buf, text, new_length);
    console_write(buf, new_length);
    if (old_length > new_length) {
        for (uint32_t i = new_length; i < old_length; i++)
            console_write(" ", 1);
        cursor_left(old_length - new_length);
    }
    *length = *position = new_length;
}

static void remember(const char *buf, uint32_t length)
{
    char line[HISTORY_WIDTH];

    if (length == 0 || length >= HISTORY_WIDTH)
        return;
    memcpy(line, buf, length);
    line[length] = 0;
    if (history_count && strcmp(history[(history_count - 1) % HISTORY_LINES], line) == 0)
        return;
    memcpy(history[history_count % HISTORY_LINES], line, length + 1);
    history_count++;
}

uint32_t console_read_line(char *buf, uint32_t size)
{
    uint32_t length = 0, position = 0;
    uint32_t browsing = history_count;
    uint32_t oldest = history_count > HISTORY_LINES ? history_count - HISTORY_LINES : 0;

    if (size < 2)
        return 0;
    current_attr = 0; /* in case the last program left colours on */

    for (;;) {
        uint8_t c = next_key();

        switch (c) {
        case '\n':
            cursor_left(0);
            console_write(buf + position, length - position);
            remember(buf, length);
            buf[length++] = '\n';
            console_write("\n", 1);
            return length;
        case '\b':
            if (position > 0) {
                cursor_left(1);
                position--;
                memmove(buf + position, buf + position + 1, length - position - 1);
                length--;
                redraw_tail(buf, position, length, 1);
            }
            break;
        case BS_KEY_DEL:
            if (position < length) {
                memmove(buf + position, buf + position + 1, length - position - 1);
                length--;
                redraw_tail(buf, position, length, 1);
            }
            break;
        case BS_KEY_LEFT:
            if (position > 0) {
                cursor_left(1);
                position--;
            }
            break;
        case BS_KEY_RIGHT:
            if (position < length) {
                console_write(buf + position, 1);
                position++;
            }
            break;
        case BS_KEY_HOME:
            cursor_left(position);
            position = 0;
            break;
        case BS_KEY_END:
            console_write(buf + position, length - position);
            position = length;
            break;
        case BS_KEY_UP:
            if (browsing > oldest) {
                browsing--;
                replace_line(buf, size - 1, &length, &position, history[browsing % HISTORY_LINES]);
            }
            break;
        case BS_KEY_DOWN:
            if (browsing < history_count) {
                browsing++;
                replace_line(buf, size - 1, &length, &position,
                             browsing == history_count ? "" : history[browsing % HISTORY_LINES]);
            }
            break;
        default:
            if (c >= 0x20 && c < 0x7F && length + 1 < size - 1) {
                memmove(buf + position + 1, buf + position, length - position);
                buf[position] = (char)c;
                length++;
                console_write(buf + position, 1);
                position++;
                redraw_tail(buf, position, length, 0);
            }
            break;
        }
    }
}
