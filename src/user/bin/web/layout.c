/* Lays styled nodes out into positioned items: block flow, inline flow, tables. */
#include "bs.h"
#include "render.h"
#include "ui.h"

#define MAX_ITEMS 8000
#define LINE_GAP 2
#define BULLET_OFFSET 10
#define MAX_COLUMNS 32
#define URL_POOL_ITEMS 512

struct item render_items[MAX_ITEMS];
int render_item_count;
int render_height;
unsigned int render_background;

static char link_urls[URL_POOL_ITEMS][WEB_URL_MAX];
static int link_count;
static int previous_margin;

struct flow {
    int left, width, y;
    int x;
    int line_start;
    int align;
};

static struct item *new_item(int kind)
{
    struct item *it;

    if (render_item_count == MAX_ITEMS)
        return 0;
    it = &render_items[render_item_count++];
    memset(it, 0, sizeof(*it));
    it->kind = (unsigned char)kind;
    return it;
}

static void flush_line(struct flow *f)
{
    int height = 0, shift;

    if (render_item_count == f->line_start) {
        f->x = f->left;
        return;
    }
    for (int i = f->line_start; i < render_item_count; i++)
        if (render_items[i].height > height)
            height = render_items[i].height;

    shift = f->align == ALIGN_CENTER ? (f->width - (f->x - f->left)) / 2
          : f->align == ALIGN_RIGHT ? f->width - (f->x - f->left) : 0;
    if (shift < 0)
        shift = 0;

    for (int i = f->line_start; i < render_item_count; i++) {
        render_items[i].x += shift;
        render_items[i].y = f->y + height - render_items[i].height;
    }
    f->y += height + LINE_GAP;
    f->x = f->left;
    f->line_start = render_item_count;
}

static int line_empty(const struct flow *f)
{
    return f->x == f->left && render_item_count == f->line_start;
}

static void place_word(struct flow *f, const struct style *s, const char *text, unsigned int length,
                       const char *link, int with_space)
{
    int char_width = GLYPH_W * s->scale;
    int word_width = (int)length * char_width;
    int space = with_space && !line_empty(f) ? char_width : 0;
    struct item *previous;

    if (!line_empty(f) && f->x + space + word_width > f->left + f->width) {
        flush_line(f);
        space = 0;
    }

    /* Extend the previous run when it is the same style on the same line. */
    previous = render_item_count > f->line_start ? &render_items[render_item_count - 1] : 0;
    if (previous && previous->kind == ITEM_TEXT && previous->color == s->color && previous->scale == s->scale &&
        previous->bold == s->bold && previous->underline == s->underline && previous->link == link && space &&
        previous->text + previous->length + 1 == text) {
        previous->length += length + 1;
        previous->width = (short)(previous->width + space + word_width);
        f->x += space + word_width;
        return;
    }

    f->x += space;
    {
        struct item *it = new_item(ITEM_TEXT);

        if (!it)
            return;
        it->x = f->x;
        it->width = (short)word_width;
        it->height = (short)(GLYPH_H * s->scale);
        it->color = s->color;
        it->scale = s->scale;
        it->bold = s->bold;
        it->underline = s->underline;
        it->text = text;
        it->length = length;
        it->link = link;
    }
    f->x += word_width;
}

static void place_text(struct flow *f, const struct node *n, const struct style *s, const char *link)
{
    const char *text = n->text;
    unsigned int length = n->text_length, i = 0;
    int space_before = 0;

    while (i < length) {
        unsigned int start;

        if (text[i] == ' ') {
            space_before = 1;
            i++;
            continue;
        }
        start = i;
        while (i < length && text[i] != ' ')
            i++;
        place_word(f, s, text + start, i - start, link, space_before || (start == 0 && 0));
        space_before = 0;
    }
    if (length && text[length - 1] == ' ' && !line_empty(f))
        f->x += GLYPH_W * s->scale;
}

static const char *resolve_link(const char *href)
{
    if (!href || link_count == URL_POOL_ITEMS)
        return 0;
    web_resolve_url(page.url, href, link_urls[link_count]);
    return link_urls[link_count++];
}

static void place_image(struct flow *f, const struct node *n, const char *link)
{
    if (n->image >= 0) {
        const struct picture *p = &render_pictures[n->image];
        struct item *it;

        if (!line_empty(f) && f->x + p->width > f->left + f->width)
            flush_line(f);
        it = new_item(ITEM_IMAGE);
        if (!it)
            return;
        it->x = f->x;
        it->width = (short)p->width;
        it->height = (short)p->height;
        it->pixels = p->pixels;
        it->link = link;
        f->x += p->width + 2;
    }
}

static int layout_box(int index, int x, int y, int width);
static void flow_children(struct flow *f, int parent, const char *link);

static int is_block(const struct node *n)
{
    if (n->kind != NODE_ELEMENT)
        return 0;
    return n->style.display != DISPLAY_INLINE && n->style.display != DISPLAY_NONE;
}

static void flow_node(struct flow *f, int index, const char *link)
{
    const struct node *n = &render_nodes[index];

    if (n->kind == NODE_TEXT) {
        place_text(f, n, &n->style, link);
        return;
    }
    if (n->style.display == DISPLAY_NONE)
        return;
    if (is_block(n)) {
        flush_line(f);
        f->y = layout_box(index, f->left, f->y, f->width);
        f->x = f->left;
        f->line_start = render_item_count;
        return;
    }

    if (strcmp(n->tag, "br") == 0) {
        if (line_empty(f))
            f->y += GLYPH_H * n->style.scale + LINE_GAP;
        else
            flush_line(f);
        return;
    }
    if (strcmp(n->tag, "img") == 0) {
        place_image(f, n, link);
        return;
    }
    if (n->href)
        link = resolve_link(n->href);
    flow_children(f, index, link);
}

static void flow_children(struct flow *f, int parent, const char *link)
{
    for (int child = render_nodes[parent].first; child >= 0; child = render_nodes[child].next)
        flow_node(f, child, link);
}

static int layout_contents(int index, int x, int y, int width)
{
    struct flow f;

    f.left = f.x = x;
    f.width = width;
    f.y = y;
    f.line_start = render_item_count;
    f.align = render_nodes[index].style.align;
    flow_children(&f, index, 0);
    flush_line(&f);
    return f.y;
}

/* A table row: cells share the width evenly and take the tallest height. */
static int layout_row(int index, int x, int y, int width)
{
    int cells = 0, column, bottom = y;

    for (int c = render_nodes[index].first; c >= 0; c = render_nodes[c].next)
        if (render_nodes[c].kind == NODE_ELEMENT && render_nodes[c].style.display == DISPLAY_TABLE_CELL)
            cells++;
    if (cells == 0)
        return layout_contents(index, x, y, width);
    if (cells > MAX_COLUMNS)
        cells = MAX_COLUMNS;

    column = width / cells;
    {
        int i = 0;

        for (int c = render_nodes[index].first; c >= 0 && i < cells; c = render_nodes[c].next) {
            int end;

            if (render_nodes[c].kind != NODE_ELEMENT || render_nodes[c].style.display != DISPLAY_TABLE_CELL)
                continue;
            previous_margin = 0;
            end = layout_box(c, x + i * column, y, column);
            if (end > bottom)
                bottom = end;
            i++;
        }
    }
    return bottom;
}

static int layout_box(int index, int x, int y, int width)
{
    const struct node *n = &render_nodes[index];
    const struct style *s = &n->style;
    int top_margin = s->margin[0], box_x, box_y, box_width, inner_x, inner_width, content_y, end;
    int rect_index = -1;
    int border = s->border ? 1 : 0;

    /* Vertical margins between siblings collapse to the larger one. */
    if (top_margin > previous_margin)
        y += top_margin - previous_margin;

    box_x = x + s->margin[3];
    box_y = y;
    box_width = width - s->margin[3] - s->margin[1];
    if (s->width >= 0 && s->width < box_width)
        box_width = s->width;
    else if (s->width_percent >= 0)
        box_width = width * s->width_percent / 100 - s->margin[3] - s->margin[1];
    if (box_width < GLYPH_W * 4)
        box_width = GLYPH_W * 4;

    if (s->has_background || border) {
        struct item *it = new_item(ITEM_RECT);

        if (it) {
            rect_index = render_item_count - 1;
            it->x = box_x;
            it->y = box_y;
            it->width = (short)box_width;
            it->color = s->background;
            it->bold = s->has_background;
            it->underline = (unsigned char)border;
            it->scale = 0;
            it->length = s->border_color;
        }
    }

    inner_x = box_x + s->padding[3] + border;
    inner_width = box_width - s->padding[3] - s->padding[1] - 2 * border;
    if (inner_width < GLYPH_W * 2)
        inner_width = GLYPH_W * 2;
    content_y = box_y + s->padding[0] + border;

    if (strcmp(n->tag, "hr") == 0) {
        end = box_y + 1;
    } else {
        previous_margin = 0;
        if (s->display == DISPLAY_LIST_ITEM) {
            struct item *bullet = new_item(ITEM_RECT);

            if (bullet) {
                bullet->x = inner_x - BULLET_OFFSET;
                bullet->y = content_y + 2 * s->scale;
                bullet->width = bullet->height = (short)(3 * s->scale);
                bullet->color = s->color;
                bullet->bold = 1;
            }
        }
        if (s->display == DISPLAY_TABLE_ROW)
            end = layout_row(index, inner_x, content_y, inner_width);
        else
            end = layout_contents(index, inner_x, content_y, inner_width);
        end += s->padding[2] + border;
    }

    if (rect_index >= 0)
        render_items[rect_index].height = (short)(end - box_y);

    previous_margin = s->margin[2];
    return end + s->margin[2];
}

/* The page colour comes from html or body. */
void render_find_background(void)
{
    render_background = 0xFFFFFF;
    for (int i = 0; i < render_node_count; i++) {
        const struct node *n = &render_nodes[i];

        if (n->kind == NODE_ELEMENT && n->style.has_background &&
            (strcmp(n->tag, "html") == 0 || strcmp(n->tag, "body") == 0))
            render_background = n->style.background;
    }
}

int page_width = 626;

void render_layout(void)
{
    int root = -1;

    render_item_count = 0;
    link_count = 0;
    previous_margin = 0;

    for (int i = 0; i < render_node_count && root < 0; i++)
        if (render_nodes[i].kind == NODE_ELEMENT && render_nodes[i].parent < 0)
            root = i;
    if (root < 0) {
        render_height = 0;
        return;
    }
    render_height = layout_box(root, 0, 0, PAGE_WIDTH);
}
