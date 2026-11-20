/* The page engine behind the browser's mouse mode: HTML, CSS, layout, images. */
#ifndef WEB_RENDER_H
#define WEB_RENDER_H

extern int page_width; /* the screen, less the scrollbar; set by the graphical mode */
#define PAGE_WIDTH page_width
#define GLYPH_W 6
#define GLYPH_H 8

enum item_kind { ITEM_TEXT, ITEM_RECT, ITEM_IMAGE };

/* One thing to draw, positioned in page coordinates. */
struct item {
    unsigned char kind, scale, bold, underline;
    int x, y;
    short width, height;
    unsigned int color;
    const char *text; /* ITEM_TEXT */
    unsigned int length;
    const char *link; /* where a click goes, or 0 */
    const unsigned int *pixels; /* ITEM_IMAGE */
};

extern struct item render_items[];
extern int render_item_count;
extern int render_height;
extern unsigned int render_background;

/* Rebuilds the items from the page just loaded. progress may be called with status text. */
void render_build(void (*progress)(const char *status));

/* ---- Internal, shared between the engine's files ------------------------ */

enum display { DISPLAY_INLINE, DISPLAY_BLOCK, DISPLAY_NONE, DISPLAY_LIST_ITEM, DISPLAY_TABLE_ROW, DISPLAY_TABLE_CELL };
enum { ALIGN_LEFT, ALIGN_CENTER, ALIGN_RIGHT };

struct style {
    unsigned int color, background, border_color;
    unsigned char has_background, scale, bold, underline, align, display, border;
    short margin[4], padding[4]; /* top, right, bottom, left */
    short width;                 /* -1 auto */
    signed char width_percent;   /* -1 unset */
};

enum node_kind { NODE_ELEMENT, NODE_TEXT };

struct node {
    unsigned char kind;
    short parent, first, last, next;
    const char *tag, *id, *classes, *href, *src, *inline_style, *text;
    unsigned short text_length;
    short attr_width, attr_height;
    short image;
    struct style style;
};

extern struct node render_nodes[];
extern int render_node_count;

const char *render_store(const char *text, unsigned int length);
void render_parse_css(const char *css, int length);
void render_apply_styles(void);
void render_css_reset(void);
void render_layout(void);
void render_find_background(void);

struct picture {
    const unsigned int *pixels;
    int width, height;
};
extern struct picture render_pictures[];
extern int render_picture_count;
void render_images_reset(void);
/* Decodes PNG or JPEG data; returns a picture index, or -1. */
int render_decode_image(const unsigned char *data, int length, int wanted_width, int wanted_height);

#endif
