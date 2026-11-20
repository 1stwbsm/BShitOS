/* The browser's mouse-driven interface, drawing the page engine's output. */
#include <bshit/font5x7.h>

#include "bs.h"
#include "render.h"
#include "ui.h"

/* The screen it was given: all of it, or a BDeskUI window. Set on entry. */
#define MAX_W 1400
#define MAX_H 900
static int screen_w = 640, screen_h = 400;
#define SCREEN_W screen_w
#define SCREEN_H screen_h
#define CHAR_W 6
#define BAR_H 24
#define STATUS_H 14
#define SCROLLBAR_W 14
#define BODY_TOP BAR_H
#define BODY_BOTTOM (SCREEN_H - STATUS_H)
#define BODY_H (BODY_BOTTOM - BODY_TOP)
#define SCROLLBAR_X (SCREEN_W - SCROLLBAR_W)
#define MIN_THUMB 16
#define FRAME_MS 15
#define LINE_STEP 24
#define KEY_ESCAPE 0x1B

#define COLOR_BAR 0x262C34u
#define COLOR_FIELD 0x0A0D10u
#define COLOR_TEXT 0xE4E6EAu
#define COLOR_DIM 0x8A92A0u
#define COLOR_HOVER 0xFFD060u
#define COLOR_BUTTON 0x3A424Eu
#define COLOR_BUTTON_HOT 0x56647Au
#define COLOR_THUMB 0x5A6472u
#define COLOR_FOCUS 0xFFD060u

struct button {
    int x, w;
    const char *label;
};

enum button_id { BUTTON_BACK, BUTTON_HOME, BUTTON_TEXT, BUTTON_QUIT, BUTTON_COUNT };

static struct button buttons[BUTTON_COUNT] = {
    [BUTTON_BACK] = {4, 40, "Back"},
    [BUTTON_HOME] = {48, 40, "Home"},
    [BUTTON_TEXT] = {640 - 88, 40, "Text"},
    [BUTTON_QUIT] = {640 - 44, 40, "Quit"},
};

#define ADDRESS_X 92
#define ADDRESS_W (SCREEN_W - 88 - ADDRESS_X - 4)
#define ADDRESS_COLUMNS ((ADDRESS_W - 8) / CHAR_W)

static const char *const pointer[] = {
    "X          ",
    "XX         ",
    "X.X        ",
    "X..X       ",
    "X...X      ",
    "X....X     ",
    "X.....X    ",
    "X......X   ",
    "X.......X  ",
    "X........X ",
    "X.....XXXXX",
    "X..X..X    ",
    "X.X X..X   ",
    "XX  X..X   ",
    "X    X..X  ",
    "     XXXX  ",
};

static unsigned int frame[MAX_W * MAX_H]; /* untouched pages take no memory */
static char address[WEB_URL_MAX];
static char rendered_url[WEB_URL_MAX];
static int address_length, address_focused;
static int mouse_x = 320, mouse_y = 200;
static int dragging_thumb;
static int scroll_y;

static void fill(int x, int y, int w, int h, unsigned int color)
{
    int right = x + w > SCREEN_W ? SCREEN_W : x + w, bottom = y + h > SCREEN_H ? SCREEN_H : y + h;

    for (int row = y < 0 ? 0 : y; row < bottom; row++)
        for (int column = x < 0 ? 0 : x; column < right; column++)
            frame[row * SCREEN_W + column] = color;
}

/* Glyphs drawn into the page area stop at its edges. */
static void draw_glyph(int x, int y, char c, int scale, unsigned int color, int clip_top, int clip_bottom)
{
    const unsigned char *glyph;

    if (c <= 0x20 || c > 0x7E)
        return;
    glyph = bs_font5x7[c - 0x20];
    for (int column = 0; column < 5; column++) {
        for (int row = 0; row < 7; row++) {
            if (!(glyph[column] & (1 << row)))
                continue;
            for (int sy = 0; sy < scale; sy++) {
                int py = y + row * scale + sy;

                if (py < clip_top || py >= clip_bottom)
                    continue;
                for (int sx = 0; sx < scale; sx++) {
                    int px = x + column * scale + sx;

                    if (px >= 0 && px < SCROLLBAR_X)
                        frame[py * SCREEN_W + px] = color;
                }
            }
        }
    }
}

static void draw_text(int x, int y, const char *s, int max_columns, unsigned int color)
{
    for (int i = 0; s[i] && i < max_columns; i++) {
        const unsigned char *glyph;

        if (s[i] <= 0x20 || s[i] > 0x7E)
            continue;
        glyph = bs_font5x7[s[i] - 0x20];
        for (int column = 0; column < 5; column++)
            for (int row = 0; row < 7; row++)
                if (glyph[column] & (1 << row))
                    fill(x + i * CHAR_W + column, y + row, 1, 1, color);
    }
}

static int inside(int x, int y, int w, int h)
{
    return mouse_x >= x && mouse_x < x + w && mouse_y >= y && mouse_y < y + h;
}

static const struct item *item_under_mouse(void)
{
    int page_x = mouse_x, page_y = mouse_y - BODY_TOP + scroll_y;

    if (mouse_y < BODY_TOP || mouse_y >= BODY_BOTTOM || mouse_x >= SCROLLBAR_X)
        return 0;
    for (int i = render_item_count - 1; i >= 0; i--) {
        const struct item *it = &render_items[i];

        if (it->link && page_x >= it->x && page_x < it->x + it->width && page_y >= it->y && page_y < it->y + it->height)
            return it;
    }
    return 0;
}

static void clamp_scroll(void)
{
    if (scroll_y + BODY_H > render_height)
        scroll_y = render_height - BODY_H;
    if (scroll_y < 0)
        scroll_y = 0;
}

static void set_address(const char *url)
{
    address_length = (int)strlen(url);
    if (address_length >= WEB_URL_MAX)
        address_length = WEB_URL_MAX - 1;
    memcpy(address, url, (unsigned int)address_length);
    address[address_length] = 0;
}

static void draw_toolbar(void)
{
    int shown_from = address_length > ADDRESS_COLUMNS ? address_length - ADDRESS_COLUMNS : 0;

    fill(0, 0, SCREEN_W, BAR_H, COLOR_BAR);
    for (int i = 0; i < BUTTON_COUNT; i++) {
        const struct button *b = &buttons[i];

        fill(b->x, 3, b->w, BAR_H - 6, inside(b->x, 3, b->w, BAR_H - 6) ? COLOR_BUTTON_HOT : COLOR_BUTTON);
        draw_text(b->x + (b->w - (int)strlen(b->label) * CHAR_W) / 2, 8, b->label, 8, COLOR_TEXT);
    }

    fill(ADDRESS_X, 3, ADDRESS_W, BAR_H - 6, COLOR_FIELD);
    if (address_focused) {
        fill(ADDRESS_X, 3, ADDRESS_W, 1, COLOR_FOCUS);
        fill(ADDRESS_X, BAR_H - 4, ADDRESS_W, 1, COLOR_FOCUS);
    }
    draw_text(ADDRESS_X + 4, 8, address + shown_from, ADDRESS_COLUMNS, COLOR_TEXT);
    if (address_focused)
        fill(ADDRESS_X + 4 + (address_length - shown_from) * CHAR_W, 6, 1, 12, COLOR_FOCUS);
}

static void draw_item(const struct item *it, const char *hovered_link)
{
    int top = it->y - scroll_y + BODY_TOP;

    if (top >= BODY_BOTTOM || top + it->height <= BODY_TOP)
        return;

    if (it->kind == ITEM_RECT) {
        int y = top < BODY_TOP ? BODY_TOP : top;
        int bottom = top + it->height > BODY_BOTTOM ? BODY_BOTTOM : top + it->height;

        if (it->bold)
            fill(it->x, y, it->width, bottom - y, it->color);
        if (it->underline) {
            if (top >= BODY_TOP)
                fill(it->x, top, it->width, 1, it->length);
            if (top + it->height - 1 < BODY_BOTTOM)
                fill(it->x, top + it->height - 1, it->width, 1, it->length);
            fill(it->x, y, 1, bottom - y, it->length);
            fill(it->x + it->width - 1, y, 1, bottom - y, it->length);
        }
    } else if (it->kind == ITEM_IMAGE) {
        for (int row = 0; row < it->height; row++) {
            int y = top + row;

            if (y < BODY_TOP || y >= BODY_BOTTOM)
                continue;
            for (int column = 0; column < it->width; column++) {
                int x = it->x + column;

                if (x >= 0 && x < SCROLLBAR_X)
                    frame[y * SCREEN_W + x] = it->pixels[row * it->width + column];
            }
        }
    } else {
        unsigned int color = hovered_link && it->link == hovered_link ? 0xD04000u : it->color;
        int advance = GLYPH_W * it->scale;

        for (unsigned int i = 0; i < it->length; i++) {
            int x = it->x + (int)i * advance;

            draw_glyph(x, top, it->text[i], it->scale, color, BODY_TOP, BODY_BOTTOM);
            if (it->bold)
                draw_glyph(x + 1, top, it->text[i], it->scale, color, BODY_TOP, BODY_BOTTOM);
        }
        if (it->underline) {
            int line = top + it->height - 1;

            if (line >= BODY_TOP && line < BODY_BOTTOM)
                fill(it->x, line, it->width, 1, color);
        }
    }
}

static void draw_body(void)
{
    const struct item *hovered = item_under_mouse();

    fill(0, BODY_TOP, SCROLLBAR_X, BODY_H, render_background);
    for (int i = 0; i < render_item_count; i++)
        draw_item(&render_items[i], hovered ? hovered->link : 0);
}

static void thumb_geometry(int *y, int *height)
{
    int total = render_height > BODY_H ? render_height : BODY_H;

    *height = BODY_H * BODY_H / total;
    if (*height < MIN_THUMB)
        *height = MIN_THUMB;
    *y = BODY_TOP + (total > BODY_H ? (BODY_H - *height) * scroll_y / (total - BODY_H) : 0);
}

static void draw_scrollbar(void)
{
    int y, height;

    fill(SCROLLBAR_X, BODY_TOP, SCROLLBAR_W, BODY_H, COLOR_BAR);
    thumb_geometry(&y, &height);
    fill(SCROLLBAR_X + 2, y, SCROLLBAR_W - 4, height, COLOR_THUMB);
}

static void draw_status(void)
{
    const struct item *hovered = item_under_mouse();

    fill(0, BODY_BOTTOM, SCREEN_W, STATUS_H, COLOR_BAR);
    draw_text(4, BODY_BOTTOM + 4, hovered ? hovered->link : page.title, (SCREEN_W - 8) / CHAR_W,
              hovered ? COLOR_HOVER : COLOR_DIM);
}

static void draw_pointer(void)
{
    for (unsigned int row = 0; row < sizeof(pointer) / sizeof(pointer[0]); row++) {
        for (int column = 0; pointer[row][column]; column++) {
            char c = pointer[row][column];

            if (c == 'X')
                fill(mouse_x + column, mouse_y + (int)row, 1, 1, 0x000000);
            else if (c == '.')
                fill(mouse_x + column, mouse_y + (int)row, 1, 1, 0xFFFFFF);
        }
    }
}

static void present(void)
{
    draw_toolbar();
    draw_body();
    draw_scrollbar();
    draw_status();
    draw_pointer();
    fb_blit(frame, SCREEN_W, SCREEN_H);
}

static void show_status(const char *text)
{
    draw_toolbar();
    fill(0, BODY_BOTTOM, SCREEN_W, STATUS_H, COLOR_BAR);
    draw_text(4, BODY_BOTTOM + 4, text, (SCREEN_W - 8) / CHAR_W, COLOR_HOVER);
    fb_blit(frame, SCREEN_W, SCREEN_H);
}

/* Lays out whichever page was loaded last. */
static void refresh_page(void)
{
    set_address(page.url);
    show_status("laying out the page...");
    render_build(show_status);
    memcpy(rendered_url, page.url, WEB_URL_MAX);
    scroll_y = 0;
}

static void go_to(const char *url)
{
    char target[WEB_URL_MAX];

    set_address(url);
    memcpy(target, address, (unsigned int)address_length + 1);
    address_focused = 0;
    show_status("loading...");
    web_navigate(target, 1);
    refresh_page();
}

static void run_and_refresh(void (*action)(void))
{
    address_focused = 0;
    show_status("loading...");
    action();
    refresh_page();
}

static void drag_thumb(void)
{
    int y, height, scrollable = render_height - BODY_H;

    thumb_geometry(&y, &height);
    if (scrollable <= 0 || BODY_H <= height)
        return;
    scroll_y = (mouse_y - BODY_TOP - height / 2) * scrollable / (BODY_H - height);
    clamp_scroll();
}

/* Returns 1 if a button asked to leave the mouse UI. */
static int press(enum graphical_result *result)
{
    const struct item *hit;

    for (int i = 0; i < BUTTON_COUNT; i++) {
        if (!inside(buttons[i].x, 3, buttons[i].w, BAR_H - 6))
            continue;
        if (i == BUTTON_BACK) {
            run_and_refresh(web_back);
        } else if (i == BUTTON_HOME) {
            run_and_refresh(web_go_home);
        } else {
            *result = i == BUTTON_TEXT ? BACK_TO_TEXT : QUIT_BROWSER;
            return 1;
        }
        return 0;
    }

    if (inside(ADDRESS_X, 3, ADDRESS_W, BAR_H - 6)) {
        address_focused = 1;
        return 0;
    }
    address_focused = 0;

    if (inside(SCROLLBAR_X, BODY_TOP, SCROLLBAR_W, BODY_H)) {
        dragging_thumb = 1;
        drag_thumb();
        return 0;
    }

    hit = item_under_mouse();
    if (hit) {
        char target[WEB_URL_MAX];

        memcpy(target, hit->link, strlen(hit->link) + 1);
        go_to(target);
    }
    return 0;
}

/* Returns 1 if the key asked to leave the mouse UI. */
static int handle_key(int key, enum graphical_result *result)
{
    if (address_focused) {
        if (key == '\n') {
            char target[WEB_URL_MAX];

            memcpy(target, address, (unsigned int)address_length + 1);
            go_to(target);
        } else if (key == '\b') {
            if (address_length > 0)
                address[--address_length] = 0;
        } else if (key == KEY_ESCAPE) {
            address_focused = 0;
            set_address(page.url);
        } else if (key >= 0x20 && key < 0x7F && address_length + 1 < WEB_URL_MAX) {
            address[address_length++] = (char)key;
            address[address_length] = 0;
        }
        return 0;
    }

    switch (key) {
    case 'p':
    case 'P':
        *result = BACK_TO_TEXT;
        return 1;
    case 'q':
        *result = QUIT_BROWSER;
        return 1;
    case 'b':
        run_and_refresh(web_back);
        break;
    case 'h':
        run_and_refresh(web_go_home);
        break;
    case BS_KEY_DOWN:
        scroll_y += LINE_STEP;
        break;
    case BS_KEY_UP:
        scroll_y -= LINE_STEP;
        break;
    case BS_KEY_PGDN:
    case ' ':
        scroll_y += BODY_H - LINE_STEP;
        break;
    case BS_KEY_PGUP:
        scroll_y -= BODY_H - LINE_STEP;
        break;
    default:
        break;
    }
    clamp_scroll();
    return 0;
}

/*
 * Sizes the page to the screen: 2x on big screens (as BDeskUI draws), 1x on
 * small ones or small windows. A width change means laying the page out again.
 */
static void fit_screen(void)
{
    struct bs_sysinfo info;
    int scale, old_width = page_width;

    sysinfo(&info);
    scale = info.screen_width >= 1000 && info.screen_height >= 600 ? 2 : 1;
    screen_w = (int)info.screen_width / scale;
    screen_h = (int)info.screen_height / scale;
    screen_w = screen_w > MAX_W ? MAX_W : screen_w < 320 ? 320 : screen_w;
    screen_h = screen_h > MAX_H ? MAX_H : screen_h < 200 ? 200 : screen_h;
    buttons[BUTTON_TEXT].x = SCREEN_W - 88;
    buttons[BUTTON_QUIT].x = SCREEN_W - 44;
    page_width = SCREEN_W - SCROLLBAR_W;
    if (page_width != old_width)
        rendered_url[0] = 0; /* laid out for another width */
    if (mouse_x >= SCREEN_W || mouse_y >= SCREEN_H) {
        mouse_x = SCREEN_W / 2;
        mouse_y = SCREEN_H / 2;
    }
}

enum graphical_result web_graphical(void)
{
    enum graphical_result result = BACK_TO_TEXT;
    struct bs_mouse stale;
    unsigned int buttons_down = 0;
    int dirty = 1;

    fit_screen();
    mouse_read(&stale); /* discard motion from before we started */
    set_address(page.url);
    address_focused = dragging_thumb = 0;
    fill(0, 0, SCREEN_W, SCREEN_H, 0xFFFFFF);
    if (strcmp(rendered_url, page.url) != 0)
        refresh_page();
    clamp_scroll();

    for (;;) {
        struct bs_mouse mouse;
        int key;

        mouse_read(&mouse);
        if (mouse.dx || mouse.dy || mouse.buttons != buttons_down) {
            int pressed = (mouse.buttons & BS_MOUSE_LEFT) && !(buttons_down & BS_MOUSE_LEFT);

            mouse_x += mouse.dx;
            mouse_y += mouse.dy;
            if (mouse_x < 0)
                mouse_x = 0;
            if (mouse_y < 0)
                mouse_y = 0;
            if (mouse_x >= SCREEN_W)
                mouse_x = SCREEN_W - 1;
            if (mouse_y >= SCREEN_H)
                mouse_y = SCREEN_H - 1;
            buttons_down = mouse.buttons;

            if (pressed && press(&result))
                return result;
            if (!(buttons_down & BS_MOUSE_LEFT))
                dragging_thumb = 0;
            else if (dragging_thumb)
                drag_thumb();
            dirty = 1;
        }

        while ((key = poll_key()) != 0) {
            if (handle_key(key, &result))
                return result;
            dirty = 1;
        }

        if (dirty) {
            present();
            dirty = 0;
        }
        sleep_ms(FRAME_MS);
    }
}
