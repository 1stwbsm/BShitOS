/* Builds a tree from HTML and fetches what the page refers to. */
#include "bs.h"
#include "render.h"
#include "ui.h"

#define MAX_NODES 4096
#define STRING_POOL (384 * 1024)
#define MAX_SHEETS 6
#define MAX_IMAGES 16
#define TAG_MAX 1024
#define ATTRIBUTE_MAX 512
#define PROGRESS_MAX 40

struct node render_nodes[MAX_NODES];
int render_node_count;

static char strings[STRING_POOL];
static unsigned int strings_used;
static int current;
static char sheet_urls[MAX_SHEETS][WEB_URL_MAX];
static int sheet_count;

const char *render_store(const char *text, unsigned int length)
{
    char *out;

    if (strings_used + length + 1 > STRING_POOL)
        return 0;
    out = strings + strings_used;
    memcpy(out, text, length);
    out[length] = 0;
    strings_used += length + 1;
    return out;
}

static char lower(char c)
{
    return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c;
}

static int same_text(const char *a, const char *b)
{
    while (*a && lower(*a) == lower(*b)) {
        a++;
        b++;
    }
    return lower(*a) == lower(*b);
}

static int is_space(char c)
{
    return c == ' ' || c == '\n' || c == '\t' || c == '\r' || c == '\f';
}

static int new_node(int kind)
{
    struct node *n;
    int index;

    if (render_node_count == MAX_NODES)
        return -1;
    index = render_node_count++;
    n = &render_nodes[index];
    memset(n, 0, sizeof(*n));
    n->kind = (unsigned char)kind;
    n->parent = (short)current;
    n->first = n->last = n->next = -1;
    n->image = -1;
    n->attr_width = n->attr_height = -1;
    if (current >= 0) {
        struct node *parent = &render_nodes[current];

        if (parent->last >= 0)
            render_nodes[parent->last].next = (short)index;
        else
            parent->first = (short)index;
        parent->last = (short)index;
    }
    return index;
}

static void add_text(const char *text, int length)
{
    static char decoded[16384];
    int used = 0, pending_space = 0;
    int i = 0;
    int node;

    while (i < length && used + 2 < (int)sizeof(decoded)) {
        char c = 0;
        int consumed;

        if (is_space(text[i])) {
            pending_space = 1;
            i++;
            continue;
        }
        consumed = web_decode_character(text + i, length - i, &c);
        i += consumed;
        if (c == ' ') {
            pending_space = 1;
            continue;
        }
        if (pending_space && used > 0)
            decoded[used++] = ' ';
        else if (pending_space)
            decoded[used++] = ' ';
        pending_space = 0;
        decoded[used++] = c;
    }
    if (pending_space && used > 0)
        decoded[used++] = ' ';
    if (used == 0 || (used == 1 && decoded[0] == ' ' && current < 0))
        return;

    node = new_node(NODE_TEXT);
    if (node < 0)
        return;
    render_nodes[node].text = render_store(decoded, (unsigned int)used);
    render_nodes[node].text_length = (unsigned short)used;
    if (!render_nodes[node].text)
        render_nodes[node].text_length = 0;
}

/* Attribute values may contain &amp; and friends; URLs very often do. */
static void decode_entities(char *value)
{
    char *in = value, *out = value;
    int remaining = (int)strlen(value);

    while (remaining > 0) {
        if (*in == '&') {
            char decoded;
            int used = web_decode_character(in, remaining, &decoded);

            *out++ = decoded;
            in += used;
            remaining -= used;
        } else {
            *out++ = *in++;
            remaining--;
        }
    }
    *out = 0;
}

static int attribute(const char *tag, const char *name, char *out, unsigned int size)
{
    unsigned int name_length = strlen(name);

    for (const char *p = tag; *p;) {
        unsigned int n = 0, length = 0;
        char quote = 0;

        while (*p && !is_space(*p))
            p++;
        while (is_space(*p))
            p++;
        while (p[n] && !is_space(p[n]) && p[n] != '=')
            n++;
        if (n == 0)
            break;

        {
            int matches = n == name_length;

            for (unsigned int i = 0; matches && i < n; i++)
                matches = lower(p[i]) == name[i];
            p += n;
            while (is_space(*p))
                p++;
            if (*p != '=') {
                if (matches) {
                    out[0] = 0;
                    return 1;
                }
                continue;
            }
            p++;
            while (is_space(*p))
                p++;
            if (*p == '"' || *p == '\'')
                quote = *p++;
            while (*p && (quote ? *p != quote : !is_space(*p))) {
                if (matches && length + 1 < size)
                    out[length++] = *p;
                p++;
            }
            if (quote && *p)
                p++;
            if (matches) {
                out[length] = 0;
                decode_entities(out);
                return 1;
            }
        }
    }
    return 0;
}

static int in_list(const char *name, const char *const *list)
{
    for (; *list; list++)
        if (same_text(name, *list))
            return 1;
    return 0;
}

static const char *const VOID_TAGS[] = {"br", "hr", "img", "meta", "link", "input", "area", "base", "col", "embed", "param", "source", "track", "wbr", 0};
static const char *const CLOSES_P[] = {"address", "article", "aside", "blockquote", "center", "div", "dl", "fieldset", "footer", "form", "h1", "h2", "h3", "h4", "h5", "h6", "header", "hr", "main", "nav", "ol", "p", "pre", "section", "table", "ul", 0};

static void pop_current(void)
{
    if (current >= 0)
        current = render_nodes[current].parent;
}

static int current_is(const char *name)
{
    return current >= 0 && render_nodes[current].kind == NODE_ELEMENT && same_text(render_nodes[current].tag, name);
}

static void imply_closes(const char *name)
{
    if (in_list(name, CLOSES_P) && current_is("p"))
        pop_current();
    if (same_text(name, "li") && current_is("li"))
        pop_current();
    if (same_text(name, "option") && current_is("option"))
        pop_current();
    if (same_text(name, "td") || same_text(name, "th") || same_text(name, "tr")) {
        if (current_is("td") || current_is("th"))
            pop_current();
    }
    if (same_text(name, "tr") && current_is("tr"))
        pop_current();
}

static void close_tag(const char *name)
{
    for (int walk = current; walk >= 0; walk = render_nodes[walk].parent) {
        if (render_nodes[walk].kind == NODE_ELEMENT && same_text(render_nodes[walk].tag, name)) {
            current = render_nodes[walk].parent;
            return;
        }
    }
}

static const char *store_attribute(const char *tag, const char *name)
{
    static char value[ATTRIBUTE_MAX];

    if (!attribute(tag, name, value, sizeof(value)))
        return 0;
    return render_store(value, strlen(value));
}

static short numeric_attribute(const char *tag, const char *name)
{
    char value[16];

    if (!attribute(tag, name, value, sizeof(value)) || value[0] < '0' || value[0] > '9')
        return -1;
    return (short)atoi(value);
}

static void open_tag(const char *tag, const char *name, unsigned int name_length, int self_closing)
{
    char lowered[16];
    int node;
    struct node *n;

    if (name_length >= sizeof(lowered))
        name_length = sizeof(lowered) - 1;
    for (unsigned int i = 0; i < name_length; i++)
        lowered[i] = lower(name[i]);
    lowered[name_length] = 0;

    imply_closes(lowered);
    node = new_node(NODE_ELEMENT);
    if (node < 0)
        return;
    n = &render_nodes[node];
    n->tag = render_store(lowered, name_length);
    n->id = store_attribute(tag, "id");
    n->classes = store_attribute(tag, "class");
    n->inline_style = store_attribute(tag, "style");
    n->attr_width = numeric_attribute(tag, "width");
    n->attr_height = numeric_attribute(tag, "height");
    if (same_text(lowered, "a"))
        n->href = store_attribute(tag, "href");
    if (same_text(lowered, "img"))
        n->src = store_attribute(tag, "src");

    if (same_text(lowered, "link")) {
        char value[ATTRIBUTE_MAX];

        if (attribute(tag, "rel", value, sizeof(value)) && same_text(value, "stylesheet") && sheet_count < MAX_SHEETS &&
            attribute(tag, "href", value, sizeof(value)))
            web_resolve_url(page.url, value, sheet_urls[sheet_count++]);
    }

    if (!self_closing && !in_list(lowered, VOID_TAGS))
        current = node;
}

/* Returns the offset of the matching close tag's '<', or the end. */
static int find_close(const char *html, int position, int length, const char *name)
{
    unsigned int name_length = strlen(name);

    for (; position + 2 + (int)name_length < length; position++) {
        if (html[position] != '<' || html[position + 1] != '/')
            continue;
        {
            unsigned int i = 0;

            while (i < name_length && lower(html[position + 2 + (int)i]) == name[i])
                i++;
            if (i == name_length)
                return position;
        }
    }
    return length;
}

static int skip_past(const char *html, int position, int length, char c)
{
    while (position < length && html[position] != c)
        position++;
    return position + 1;
}

static int has_body_tag(const char *html, int length)
{
    for (int i = 0; i + 5 < length; i++)
        if (html[i] == '<' && lower(html[i + 1]) == 'b' && lower(html[i + 2]) == 'o' && lower(html[i + 3]) == 'd' &&
            lower(html[i + 4]) == 'y')
            return 1;
    return 0;
}

static void parse_html(const char *html, int length)
{
    static char tag[TAG_MAX];
    int i = 0;

    /* Everything hangs off one root, so fragments lay out like full documents. */
    current = -1;
    {
        int root = new_node(NODE_ELEMENT);

        if (root < 0)
            return;
        render_nodes[root].tag = render_store(has_body_tag(html, length) ? "bsroot" : "body", has_body_tag(html, length) ? 6 : 4);
        current = root;
    }

    while (i < length) {
        int start = i;

        if (html[i] != '<') {
            while (i < length && html[i] != '<')
                i++;
            add_text(html + start, i - start);
            continue;
        }

        if (i + 3 < length && html[i + 1] == '!' && html[i + 2] == '-' && html[i + 3] == '-') {
            for (i += 4; i + 2 < length && !(html[i] == '-' && html[i + 1] == '-' && html[i + 2] == '>'); i++)
                ;
            i += 3;
            continue;
        }
        if (i + 1 < length && (html[i + 1] == '!' || html[i + 1] == '?')) {
            i = skip_past(html, i, length, '>');
            continue;
        }

        {
            int n = 0, in_quote = 0, self_closing;
            unsigned int name_length = 0;
            int closing;

            i++;
            while (i < length && (html[i] != '>' || in_quote)) {
                char c = html[i++];

                if (c == '"' || c == '\'')
                    in_quote = in_quote == c ? 0 : in_quote ? in_quote : c;
                if (n + 1 < TAG_MAX)
                    tag[n++] = c;
            }
            i++;
            tag[n] = 0;
            self_closing = n > 0 && tag[n - 1] == '/';

            closing = tag[0] == '/';
            {
                const char *name = closing ? tag + 1 : tag;

                while (name[name_length] && !is_space(name[name_length]) && name[name_length] != '/')
                    name_length++;

                if (closing) {
                    char lowered[16];

                    if (name_length < sizeof(lowered)) {
                        memcpy(lowered, name, name_length);
                        lowered[name_length] = 0;
                        close_tag(lowered);
                    }
                } else if (name_length) {
                    char lowered[16] = {0};

                    if (name_length < sizeof(lowered))
                        for (unsigned int k = 0; k < name_length; k++)
                            lowered[k] = lower(name[k]);

                    if (same_text(lowered, "script") || same_text(lowered, "style") || same_text(lowered, "textarea") ||
                        same_text(lowered, "title")) {
                        int stop = self_closing ? i : find_close(html, i, length, lowered);

                        if (same_text(lowered, "style"))
                            render_parse_css(html + i, stop - i);
                        i = skip_past(html, stop, length, '>');
                    } else {
                        open_tag(tag, name, name_length, self_closing);
                    }
                }
            }
        }
    }
}

static void build_from_text(void)
{
    current = -1;
    {
        int root = new_node(NODE_ELEMENT);

        if (root < 0)
            return;
        render_nodes[root].tag = render_store("body", 4);
        current = root;
    }
    for (int i = 0; i < page.line_count; i++) {
        int node = new_node(NODE_ELEMENT);

        if (node < 0)
            return;
        render_nodes[node].tag = render_store("div", 3);
        current = node;
        add_text(page.text + page.lines[i].start, page.lines[i].length ? page.lines[i].length : 0);
        if (page.lines[i].length == 0)
            add_text(".", 1);
        pop_current();
    }
}

static void fetch_stylesheets(void (*progress)(const char *))
{
    for (int i = 0; i < sheet_count; i++) {
        const char *data;
        int length;

        progress("loading styles...");
        if (web_fetch(sheet_urls[i], &data, &length) == 0)
            render_parse_css(data, length);
    }
}

static void fetch_images(void (*progress)(const char *))
{
    int fetched = 0;

    for (int i = 0; i < render_node_count && fetched < MAX_IMAGES; i++) {
        struct node *n = &render_nodes[i];
        char url[WEB_URL_MAX];
        const char *data;
        int length;

        if (n->kind != NODE_ELEMENT || !n->src || n->style.display == DISPLAY_NONE)
            continue;
        web_resolve_url(page.url, n->src, url);
        progress("loading images...");
        fetched++;
        if (web_fetch(url, &data, &length) == 0)
            n->image = (short)render_decode_image((const unsigned char *)data, length, n->attr_width, n->attr_height);
    }
}

void render_build(void (*progress)(const char *status))
{
    render_node_count = 0;
    strings_used = 0;
    sheet_count = 0;
    render_css_reset();
    render_images_reset();

    if (page.error[0] || !web_is_html()) {
        build_from_text();
    } else {
        const char *html;
        int length;

        web_body(&html, &length);
        parse_html(html, length);
        fetch_stylesheets(progress);
    }
    render_apply_styles();
    render_find_background();
    fetch_images(progress);
    render_layout();
}
