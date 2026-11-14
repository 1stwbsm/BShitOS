/* A CSS subset: selectors, the cascade and the common visual properties. */
#include "bs.h"
#include "render.h"

#define MAX_RULES 1600
#define MAX_DECLS 1100
#define MAX_PARTS 4
#define MAX_MATCHES 96
#define DOC_FONT_PX 16
#define NOT_SET 32767
#define SCREEN_CSS_WIDTH 1280

/* One compound selector: tag#id.class.class */
struct part {
    const char *tag, *id, *classes[2];
};

struct rule {
    struct part parts[MAX_PARTS];
    unsigned char count;
    unsigned short specificity, decl;
};

enum {
    SET_COLOR = 1 << 0, SET_BG = 1 << 1, SET_SCALE = 1 << 2, SET_BOLD = 1 << 3, SET_UNDERLINE = 1 << 4,
    SET_ALIGN = 1 << 5, SET_DISPLAY = 1 << 6, SET_BORDER = 1 << 7, SET_BORDER_COLOR = 1 << 8,
    SET_WIDTH = 1 << 9,
};

struct decl {
    unsigned int mask;
    struct style values;
};

static struct rule rules[MAX_RULES];
static int rule_count;
static struct decl decls[MAX_DECLS];
static int decl_count;

void render_css_reset(void)
{
    rule_count = decl_count = 0;
}

static char lower(char c)
{
    return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c;
}

static int is_space(char c)
{
    return c == ' ' || c == '\n' || c == '\t' || c == '\r' || c == '\f';
}

static int starts(const char *text, const char *prefix)
{
    for (; *prefix; text++, prefix++)
        if (lower(*text) != *prefix)
            return 0;
    return 1;
}

static int text_is(const char *text, const char *word)
{
    while (*word && lower(*text) == *word) {
        text++;
        word++;
    }
    return !*word && !*text;
}

/* ---- Values ---------------------------------------------------------------- */

static int hex_value(char c)
{
    c = lower(c);
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    return -1;
}

static int parse_color(const char *v, unsigned int *out)
{
    static const struct { const char *name; unsigned int value; } names[] = {
        {"black", 0x000000}, {"white", 0xFFFFFF}, {"red", 0xFF0000}, {"green", 0x008000}, {"blue", 0x0000FF},
        {"yellow", 0xFFFF00}, {"orange", 0xFFA500}, {"purple", 0x800080}, {"pink", 0xFFC0CB}, {"gray", 0x808080},
        {"grey", 0x808080}, {"silver", 0xC0C0C0}, {"navy", 0x000080}, {"teal", 0x008080}, {"maroon", 0x800000},
        {"olive", 0x808000}, {"lime", 0x00FF00}, {"aqua", 0x00FFFF}, {"cyan", 0x00FFFF}, {"fuchsia", 0xFF00FF},
        {"magenta", 0xFF00FF}, {"brown", 0xA52A2A}, {"gold", 0xFFD700}, {"tomato", 0xFF6347}, {"coral", 0xFF7F50},
        {"darkblue", 0x00008B}, {"darkred", 0x8B0000}, {"darkgreen", 0x006400}, {"lightgray", 0xD3D3D3},
        {"lightgrey", 0xD3D3D3}, {"darkgray", 0xA9A9A9}, {"darkgrey", 0xA9A9A9}, {"lightblue", 0xADD8E6},
        {"whitesmoke", 0xF5F5F5}, {"beige", 0xF5F5DC}, {"crimson", 0xDC143C}, {"indigo", 0x4B0082}, {"violet", 0xEE82EE},
    };

    if (v[0] == '#') {
        int n = 0;

        while (hex_value(v[1 + n]) >= 0)
            n++;
        if (n == 3) {
            *out = (unsigned int)(hex_value(v[1]) * 17) << 16 | (unsigned int)(hex_value(v[2]) * 17) << 8 |
                   (unsigned int)(hex_value(v[3]) * 17);
            return 1;
        }
        if (n >= 6) {
            unsigned int value = 0;

            for (int i = 1; i <= 6; i++)
                value = value << 4 | (unsigned int)hex_value(v[i]);
            *out = value;
            return 1;
        }
        return 0;
    }
    if (starts(v, "rgb")) {
        const char *p = v;
        unsigned int parts[3];

        while (*p && *p != '(')
            p++;
        if (!*p)
            return 0;
        p++;
        for (int i = 0; i < 3; i++) {
            while (*p && (*p < '0' || *p > '9'))
                p++;
            if (!*p)
                return 0;
            parts[i] = (unsigned int)atoi(p);
            if (parts[i] > 255)
                parts[i] = 255;
            while (*p >= '0' && *p <= '9')
                p++;
            if (*p == '.')
                while (*++p >= '0' && *p <= '9')
                    ;
        }
        *out = parts[0] << 16 | parts[1] << 8 | parts[2];
        return 1;
    }
    for (unsigned int i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        if (text_is(v, names[i].name)) {
            *out = names[i].value;
            return 1;
        }
    }
    return 0;
}

/* Returns CSS pixels, or NOT_SET. Percentages are reported through *percent. */
static int parse_length(const char *v, int *percent)
{
    int negative = 0, value = 0, fraction = 0, scale = 1, seen = 0;
    const char *p = v;

    if (percent)
        *percent = -1;
    if (*p == '-') {
        negative = 1;
        p++;
    }
    while (*p >= '0' && *p <= '9') {
        value = value * 10 + (*p++ - '0');
        seen = 1;
        if (value > 20000)
            return NOT_SET;
    }
    if (*p == '.') {
        p++;
        while (*p >= '0' && *p <= '9') {
            if (scale < 1000) {
                fraction = fraction * 10 + (*p - '0');
                scale *= 10;
            }
            p++;
            seen = 1;
        }
    }
    if (!seen)
        return NOT_SET;
    if (*p == '%') {
        if (percent)
            *percent = value;
        return NOT_SET;
    }
    if (starts(p, "em") || starts(p, "rem"))
        value = value * DOC_FONT_PX + fraction * DOC_FONT_PX / scale;
    else if (starts(p, "pt"))
        value = value * 4 / 3;
    else if (*p && !starts(p, "px"))
        return NOT_SET;
    return negative ? -value : value;
}

/* The screen shows two device pixels per layout unit. */
static short to_units(int px)
{
    return (short)(px / 2);
}

static unsigned char font_scale(int px)
{
    int scale = (px + DOC_FONT_PX / 2) / DOC_FONT_PX;

    return (unsigned char)(scale < 1 ? 1 : scale > 4 ? 4 : scale);
}

static const char *next_word(const char *p, char *out, unsigned int size)
{
    unsigned int n = 0, depth = 0;

    while (is_space(*p))
        p++;
    while (*p && (!is_space(*p) || depth)) {
        if (*p == '(')
            depth++;
        if (*p == ')' && depth)
            depth--;
        if (n + 1 < size)
            out[n++] = *p;
        p++;
    }
    out[n] = 0;
    return p;
}

static void apply_box(short *sides, const char *value)
{
    char word[32];
    int v[4], n = 0;

    while (n < 4) {
        int px;

        value = next_word(value, word, sizeof(word));
        if (!word[0])
            break;
        px = text_is(word, "auto") ? 0 : parse_length(word, 0);
        if (px == NOT_SET)
            return;
        v[n++] = px;
    }
    if (n == 0)
        return;
    sides[0] = to_units(v[0]);
    sides[1] = to_units(n > 1 ? v[1] : v[0]);
    sides[2] = to_units(n > 2 ? v[2] : v[0]);
    sides[3] = to_units(n > 3 ? v[3] : n > 1 ? v[1] : v[0]);
}

static void set_side(struct style *s, short *sides, const char *name, const char *value, int base)
{
    int px = text_is(value, "auto") ? 0 : parse_length(value, 0);

    (void)s;
    (void)base;
    if (px == NOT_SET)
        return;
    if (text_is(name, "top"))
        sides[0] = to_units(px);
    else if (text_is(name, "right"))
        sides[1] = to_units(px);
    else if (text_is(name, "bottom"))
        sides[2] = to_units(px);
    else if (text_is(name, "left"))
        sides[3] = to_units(px);
}

/* Margins and padding use this sentinel to mean "not declared here". */
#define BOX_UNSET (-32768)

static void apply_property(struct decl *d, const char *name, const char *value)
{
    struct style *s = &d->values;
    unsigned int color;

    if (text_is(name, "color")) {
        if (parse_color(value, &color)) {
            s->color = color;
            d->mask |= SET_COLOR;
        }
    } else if (text_is(name, "background-color") || text_is(name, "background")) {
        char word[64];
        const char *p = value;

        for (;;) {
            p = next_word(p, word, sizeof(word));
            if (!word[0])
                break;
            if (text_is(word, "transparent") || text_is(word, "none")) {
                s->has_background = 0;
                d->mask |= SET_BG;
                break;
            }
            if (parse_color(word, &color)) {
                s->background = color;
                s->has_background = 1;
                d->mask |= SET_BG;
                break;
            }
        }
    } else if (text_is(name, "font-size")) {
        int percent, px = parse_length(value, &percent);

        if (percent >= 0)
            px = DOC_FONT_PX * percent / 100;
        else if (text_is(value, "large") || text_is(value, "larger"))
            px = 20;
        else if (text_is(value, "x-large") || text_is(value, "xx-large"))
            px = 32;
        else if (text_is(value, "small") || text_is(value, "smaller") || text_is(value, "medium"))
            px = DOC_FONT_PX;
        if (px != NOT_SET && px > 0) {
            s->scale = font_scale(px);
            d->mask |= SET_SCALE;
        }
    } else if (text_is(name, "font-weight")) {
        s->bold = text_is(value, "bold") || text_is(value, "bolder") || atoi(value) >= 600;
        d->mask |= SET_BOLD;
    } else if (text_is(name, "text-decoration") || text_is(name, "text-decoration-line")) {
        s->underline = starts(value, "underline");
        d->mask |= SET_UNDERLINE;
    } else if (text_is(name, "text-align")) {
        s->align = text_is(value, "center") ? ALIGN_CENTER : text_is(value, "right") ? ALIGN_RIGHT : ALIGN_LEFT;
        d->mask |= SET_ALIGN;
    } else if (text_is(name, "display")) {
        int known = 1;

        if (text_is(value, "none"))
            s->display = DISPLAY_NONE;
        else if (text_is(value, "inline") || text_is(value, "inline-block") || text_is(value, "inline-flex"))
            s->display = DISPLAY_INLINE;
        else if (text_is(value, "block") || text_is(value, "flex") || text_is(value, "grid"))
            s->display = DISPLAY_BLOCK;
        else if (text_is(value, "list-item"))
            s->display = DISPLAY_LIST_ITEM;
        else if (text_is(value, "table-row"))
            s->display = DISPLAY_TABLE_ROW;
        else if (text_is(value, "table-cell"))
            s->display = DISPLAY_TABLE_CELL;
        else
            known = 0;
        if (known)
            d->mask |= SET_DISPLAY;
    } else if (text_is(name, "visibility")) {
        if (text_is(value, "hidden")) {
            s->display = DISPLAY_NONE;
            d->mask |= SET_DISPLAY;
        }
    } else if (text_is(name, "margin")) {
        apply_box(s->margin, value);
    } else if (text_is(name, "padding")) {
        apply_box(s->padding, value);
    } else if (starts(name, "margin-")) {
        set_side(s, s->margin, name + 7, value, 0);
    } else if (starts(name, "padding-")) {
        set_side(s, s->padding, name + 8, value, 0);
    } else if (text_is(name, "width") || text_is(name, "max-width")) {
        int percent, px = parse_length(value, &percent);

        if (percent >= 0 && percent <= 100) {
            s->width_percent = (signed char)percent;
            s->width = -1;
            d->mask |= SET_WIDTH;
        } else if (px != NOT_SET && px >= 0) {
            s->width = to_units(px);
            s->width_percent = -1;
            d->mask |= SET_WIDTH;
        }
    } else if (text_is(name, "border") || text_is(name, "border-top") || text_is(name, "border-bottom")) {
        char word[64];
        const char *p = value;

        if (text_is(value, "none") || text_is(value, "0")) {
            s->border = 0;
            d->mask |= SET_BORDER;
            return;
        }
        for (;;) {
            p = next_word(p, word, sizeof(word));
            if (!word[0])
                break;
            if (parse_color(word, &color)) {
                s->border_color = color;
                d->mask |= SET_BORDER_COLOR;
            }
        }
        s->border = 1;
        d->mask |= SET_BORDER;
    } else if (text_is(name, "border-color")) {
        if (parse_color(value, &color)) {
            s->border_color = color;
            d->mask |= SET_BORDER_COLOR;
        }
    }
}

static void init_decl(struct decl *d)
{
    memset(d, 0, sizeof(*d));
    for (int i = 0; i < 4; i++)
        d->values.margin[i] = d->values.padding[i] = BOX_UNSET;
}

/* Parses "name: value; name: value" (not NUL-terminated). */
static void parse_declarations(struct decl *d, const char *text, int length)
{
    int i = 0;

    while (i < length) {
        char name[32], value[256];
        unsigned int n = 0, v = 0;
        int depth = 0;

        while (i < length && (is_space(text[i]) || text[i] == ';'))
            i++;
        while (i < length && text[i] != ':' && text[i] != ';' && text[i] != '}') {
            if (n + 1 < sizeof(name) && !is_space(text[i]))
                name[n++] = lower(text[i]);
            i++;
        }
        name[n] = 0;
        if (i >= length || text[i] != ':') {
            i++;
            continue;
        }
        i++;
        while (i < length && is_space(text[i]))
            i++;
        while (i < length && (text[i] != ';' || depth)) {
            if (text[i] == '(')
                depth++;
            if (text[i] == ')' && depth)
                depth--;
            if (v + 1 < sizeof(value))
                value[v++] = text[i];
            i++;
        }
        while (v && is_space(value[v - 1]))
            v--;
        value[v] = 0;
        {
            char *important = strchr(value, '!');

            if (important) {
                *important = 0;
                while (important > value && is_space(important[-1]))
                    *--important = 0;
            }
        }
        if (n && v)
            apply_property(d, name, value);
    }
}

/* ---- Selectors -------------------------------------------------------------- */

static int is_name_char(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
}

static const char *copy_name(const char *p, const char *end, const char **out)
{
    const char *start = p;

    while (p < end && is_name_char(*p))
        p++;
    *out = p > start ? render_store(start, (unsigned int)(p - start)) : 0;
    return p;
}

/* Parses one comma-free selector into r. Returns 0 if it uses unsupported syntax. */
static int parse_selector(const char *p, const char *end, struct rule *r)
{
    unsigned int specificity = 0;

    r->count = 0;
    while (p < end) {
        struct part *part;
        int class_count = 0;

        while (p < end && (is_space(*p) || *p == '>'))
            p++;
        if (p >= end)
            break;
        if (r->count == MAX_PARTS)
            return 0;
        part = &r->parts[r->count++];
        memset(part, 0, sizeof(*part));

        if (*p == '*') {
            p++;
        } else if (is_name_char(*p)) {
            p = copy_name(p, end, &part->tag);
            specificity += 1;
        }
        while (p < end && !is_space(*p) && *p != '>') {
            if (*p == '.') {
                if (class_count == 2)
                    return 0;
                p = copy_name(p + 1, end, &part->classes[class_count++]);
                specificity += 10;
            } else if (*p == '#') {
                p = copy_name(p + 1, end, &part->id);
                specificity += 100;
            } else if (*p == ':') {
                if (end - p >= 5 && starts(p, ":root")) {
                    p += 5;
                } else if (end - p >= 5 && starts(p, ":link")) {
                    p += 5;
                } else {
                    return 0; /* hover, focus and friends never apply here */
                }
            } else {
                return 0; /* attribute selectors, + and ~ */
            }
        }
    }
    r->specificity = (unsigned short)specificity;
    return r->count > 0;
}

static void add_rules(const char *selectors, int selector_length, const char *body, int body_length)
{
    struct decl *d;
    int decl_index, start = 0, added = 0;

    if (decl_count == MAX_DECLS)
        return;
    decl_index = decl_count;
    d = &decls[decl_index];
    init_decl(d);
    parse_declarations(d, body, body_length);

    for (int i = 0; i <= selector_length; i++) {
        if (i < selector_length && selectors[i] != ',')
            continue;
        if (rule_count < MAX_RULES && parse_selector(selectors + start, selectors + i, &rules[rule_count])) {
            rules[rule_count].decl = (unsigned short)decl_index;
            rule_count++;
            added = 1;
        }
        start = i + 1;
    }
    if (added)
        decl_count++;
}

/* Reads the number after "name:" in an @media query, or -1 if absent. */
static int media_number(const char *query, int length, const char *name)
{
    unsigned int name_length = strlen(name);

    for (int i = 0; i + (int)name_length < length; i++) {
        unsigned int k = 0;

        while (k < name_length && lower(query[i + (int)k]) == name[k])
            k++;
        if (k == name_length) {
            i += (int)name_length;
            while (i < length && (is_space(query[i]) || query[i] == ':'))
                i++;
            return i < length && query[i] >= '0' && query[i] <= '9' ? atoi(query + i) : -1;
        }
    }
    return -1;
}

static int media_applies(const char *query, int length)
{
    static const char print[] = "print";
    int minimum, maximum;

    if (length < 6 || !starts(query, "@media"))
        return 0;
    for (int i = 0; i + 5 <= length; i++) {
        int k = 0;

        while (k < 5 && lower(query[i + k]) == print[k])
            k++;
        if (k == 5)
            return 0;
    }
    minimum = media_number(query, length, "min-width");
    maximum = media_number(query, length, "max-width");
    return (minimum < 0 || minimum <= SCREEN_CSS_WIDTH) && (maximum < 0 || maximum >= SCREEN_CSS_WIDTH);
}

void render_parse_css(const char *css, int length)
{
    int i = 0;

    while (i < length) {
        int start;

        while (i < length && is_space(css[i]))
            i++;
        if (i + 1 < length && css[i] == '/' && css[i + 1] == '*') {
            for (i += 2; i + 1 < length && !(css[i] == '*' && css[i + 1] == '/'); i++)
                ;
            i += 2;
            continue;
        }
        if (i >= length)
            break;

        if (css[i] == '@') {
            /* @media applies when it suits this screen; other at-rules are skipped. */
            int depth = 0, at_start = i, block_start = -1;

            while (i < length) {
                if (css[i] == ';' && depth == 0) {
                    i++;
                    break;
                }
                if (css[i] == '{' && depth++ == 0)
                    block_start = i + 1;
                if (css[i] == '}' && --depth == 0) {
                    if (block_start >= 0 && media_applies(css + at_start, block_start - 1 - at_start))
                        render_parse_css(css + block_start, i - block_start);
                    i++;
                    break;
                }
                i++;
            }
            continue;
        }

        start = i;
        while (i < length && css[i] != '{')
            i++;
        if (i >= length)
            break;
        {
            int selector_end = i, body_start = ++i;

            while (i < length && css[i] != '}')
                i++;
            add_rules(css + start, selector_end - start, css + body_start, i - body_start);
            i++;
        }
    }
}

/* ---- The cascade --------------------------------------------------------------- */

static int has_class(const char *classes, const char *wanted)
{
    unsigned int length = strlen(wanted);

    while (classes && *classes) {
        unsigned int n = 0;

        while (is_space(*classes))
            classes++;
        while (classes[n] && !is_space(classes[n]))
            n++;
        if (n == length && strncmp(classes, wanted, n) == 0)
            return 1;
        classes += n;
    }
    return 0;
}

static int part_matches(const struct part *part, const struct node *n)
{
    if (part->tag && strcmp(part->tag, n->tag) != 0)
        return 0;
    if (part->id && (!n->id || strcmp(part->id, n->id) != 0))
        return 0;
    for (int i = 0; i < 2; i++)
        if (part->classes[i] && !has_class(n->classes, part->classes[i]))
            return 0;
    return 1;
}

static int rule_matches(const struct rule *r, int index)
{
    int part = r->count - 1;

    if (!part_matches(&r->parts[part], &render_nodes[index]))
        return 0;
    part--;
    for (int walk = render_nodes[index].parent; part >= 0 && walk >= 0; walk = render_nodes[walk].parent)
        if (part_matches(&r->parts[part], &render_nodes[walk]))
            part--;
    return part < 0;
}

static void merge(struct style *s, const struct decl *d)
{
    const struct style *v = &d->values;

    if (d->mask & SET_COLOR)
        s->color = v->color;
    if (d->mask & SET_BG) {
        s->background = v->background;
        s->has_background = v->has_background;
    }
    if (d->mask & SET_SCALE)
        s->scale = v->scale;
    if (d->mask & SET_BOLD)
        s->bold = v->bold;
    if (d->mask & SET_UNDERLINE)
        s->underline = v->underline;
    if (d->mask & SET_ALIGN)
        s->align = v->align;
    if (d->mask & SET_DISPLAY)
        s->display = v->display;
    if (d->mask & SET_BORDER)
        s->border = v->border;
    if (d->mask & SET_BORDER_COLOR)
        s->border_color = v->border_color;
    if (d->mask & SET_WIDTH) {
        s->width = v->width;
        s->width_percent = v->width_percent;
    }
    for (int i = 0; i < 4; i++) {
        if (v->margin[i] != BOX_UNSET)
            s->margin[i] = v->margin[i];
        if (v->padding[i] != BOX_UNSET)
            s->padding[i] = v->padding[i];
    }
}

static int tag_in(const char *tag, const char *list)
{
    unsigned int length = strlen(tag);

    while (*list) {
        const char *end = list;

        while (*end && *end != ' ')
            end++;
        if ((unsigned int)(end - list) == length && strncmp(list, tag, length) == 0)
            return 1;
        list = *end ? end + 1 : end;
    }
    return 0;
}

/* The defaults every browser applies before the page's own CSS. */
static void user_agent_style(struct style *s, const char *tag)
{
    if (tag_in(tag, "head meta link script style title base template datalist option select"))
        s->display = DISPLAY_NONE;
    else if (tag_in(tag, "bsroot html body div p h1 h2 h3 h4 h5 h6 ul ol dl dt dd pre blockquote table tbody thead tfoot "
                         "form header footer nav main section article aside center address figure figcaption hr fieldset details summary"))
        s->display = DISPLAY_BLOCK;
    else if (tag_in(tag, "li"))
        s->display = DISPLAY_LIST_ITEM;
    else if (tag_in(tag, "tr"))
        s->display = DISPLAY_TABLE_ROW;
    else if (tag_in(tag, "td th"))
        s->display = DISPLAY_TABLE_CELL;

    if (tag_in(tag, "body"))
        s->margin[0] = s->margin[1] = s->margin[2] = s->margin[3] = 4;
    if (tag_in(tag, "p ul ol dl blockquote pre"))
        s->margin[0] = s->margin[2] = 8;
    if (tag_in(tag, "ul ol"))
        s->padding[3] = 16;
    if (tag_in(tag, "blockquote"))
        s->margin[3] = s->margin[1] = 20;
    if (tag_in(tag, "h1")) {
        s->scale = 3;
        s->bold = 1;
        s->margin[0] = s->margin[2] = 10;
    } else if (tag_in(tag, "h2")) {
        s->scale = 2;
        s->bold = 1;
        s->margin[0] = s->margin[2] = 8;
    } else if (tag_in(tag, "h3 h4 h5 h6")) {
        s->bold = 1;
        s->margin[0] = s->margin[2] = 6;
    }
    if (tag_in(tag, "b strong th"))
        s->bold = 1;
    if (tag_in(tag, "center"))
        s->align = ALIGN_CENTER;
    if (tag_in(tag, "a")) {
        s->color = 0x0000EE;
        s->underline = 1;
    }
    if (tag_in(tag, "u ins"))
        s->underline = 1;
    if (tag_in(tag, "td th"))
        s->padding[0] = s->padding[1] = s->padding[2] = s->padding[3] = 2;
    if (tag_in(tag, "hr")) {
        s->border = 1;
        s->border_color = 0x808080;
        s->margin[0] = s->margin[2] = 6;
    }
}

static void inherit(struct style *s, const struct style *parent)
{
    memset(s, 0, sizeof(*s));
    s->width = -1;
    s->width_percent = -1;
    s->display = DISPLAY_INLINE;
    s->scale = 1;
    s->color = 0x000000;
    if (parent) {
        s->color = parent->color;
        s->scale = parent->scale;
        s->bold = parent->bold;
        s->align = parent->align;
        s->underline = parent->underline;
    }
}

static void style_node(int index)
{
    struct node *n = &render_nodes[index];
    struct style *parent = n->parent >= 0 ? &render_nodes[n->parent].style : 0;
    int matches[MAX_MATCHES], count = 0;

    inherit(&n->style, parent);
    if (n->kind == NODE_TEXT)
        return;
    user_agent_style(&n->style, n->tag);

    for (int r = 0; r < rule_count && count < MAX_MATCHES; r++)
        if (rule_matches(&rules[r], index))
            matches[count++] = r;

    /* Stable insertion sort: later rules win ties. */
    for (int i = 1; i < count; i++) {
        int value = matches[i], j = i - 1;

        while (j >= 0 && rules[matches[j]].specificity > rules[value].specificity) {
            matches[j + 1] = matches[j];
            j--;
        }
        matches[j + 1] = value;
    }
    for (int i = 0; i < count; i++)
        merge(&n->style, &decls[rules[matches[i]].decl]);

    if (n->inline_style) {
        static struct decl inline_decl;

        init_decl(&inline_decl);
        parse_declarations(&inline_decl, n->inline_style, (int)strlen(n->inline_style));
        merge(&n->style, &inline_decl);
    }
}

void render_apply_styles(void)
{
    /* Parents are always created before their children. */
    for (int i = 0; i < render_node_count; i++)
        style_node(i);
}
