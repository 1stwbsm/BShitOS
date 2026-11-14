#include "bs.h"
#include "tls.h"
#include "web.h"

#define BODY_MAX (512 * 1024)
#define HEADERS_MAX (64 * 1024)
#define REDIRECT_LIMIT 5
#define HOST_MAX 128
#define WORD_MAX 512
#define TAG_MAX 512
#define HTTP_PORT 80
#define HTTPS_PORT 443
#define CONF_PATH "/conf/web.conf"
#define DEFAULT_HOME "file:/conf/start.html"
#define USER_AGENT "BShitOS-web/1"

static char body[BODY_MAX];
static int body_length;

/* ---- Small helpers ---------------------------------------------------- */

static char lower(char c)
{
    return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c;
}

static int starts_with(const char *text, const char *prefix)
{
    for (; *prefix; text++, prefix++)
        if (lower(*text) != lower(*prefix))
            return 0;
    return 1;
}

static void copy_text(char *out, unsigned int size, const char *text)
{
    unsigned int n = strlen(text);

    if (n >= size)
        n = size - 1;
    memcpy(out, text, n);
    out[n] = 0;
}

static void set_error(char *error, const char *first, const char *second)
{
    unsigned int used;

    copy_text(error, WEB_ERROR_MAX, first);
    used = strlen(error);
    if (second)
        copy_text(error + used, WEB_ERROR_MAX - used, second);
}

/* ---- Fetching --------------------------------------------------------- */

static int parse_ip(const char *text, unsigned int *ip)
{
    unsigned int value = 0, part = 0, parts = 0;
    int digits = 0;

    for (;; text++) {
        if (*text >= '0' && *text <= '9') {
            part = part * 10 + (unsigned int)(*text - '0');
            digits++;
            if (part > 255)
                return 0;
        } else if ((*text == '.' || !*text) && digits) {
            value = value << 8 | part;
            part = 0;
            digits = 0;
            parts++;
            if (!*text)
                break;
        } else {
            return 0;
        }
    }
    if (parts != 4)
        return 0;
    *ip = value;
    return 1;
}

static int resolve_host(const char *host, unsigned int *ip)
{
    return parse_ip(host, ip) ? 0 : dns_lookup(host, ip);
}

static int use_tls;

static int send_all(int connection, const char *text)
{
    return use_tls ? tls_send(text, strlen(text)) : tcp_send(connection, text, strlen(text));
}

static int receive_some(int connection, void *buf, unsigned int size)
{
    return use_tls ? tls_receive(buf, size) : tcp_receive(connection, buf, size);
}

static void close_transport(int connection)
{
    if (use_tls)
        tls_close();
    tcp_close(connection);
}

/* Decodes a chunked body in place. */
static void dechunk(void)
{
    int in = 0, out = 0;

    while (in < body_length) {
        unsigned int size = 0;

        for (; in < body_length && body[in] != '\r' && body[in] != '\n' && body[in] != ';'; in++) {
            char c = lower(body[in]);
            unsigned int digit = c >= 'a' && c <= 'f' ? (unsigned int)(c - 'a' + 10) : (unsigned int)(c - '0');

            size = size * 16 + digit;
        }
        while (in < body_length && body[in] != '\n')
            in++;
        in++;
        if (size == 0 || in + (int)size > body_length)
            break;
        for (unsigned int i = 0; i < size; i++)
            body[out++] = body[in + (int)i];
        in += (int)size + 2;
    }
    body_length = out;
}

static const char *find_header(const char *headers, const char *name)
{
    for (const char *line = headers; line && *line;) {
        const char *end = strchr(line, '\n');

        if (starts_with(line, name)) {
            line += strlen(name);
            while (*line == ' ')
                line++;
            return line;
        }
        line = end ? end + 1 : 0;
    }
    return 0;
}

static void header_value(const char *value, char *out, unsigned int size)
{
    unsigned int n = 0;

    while (value[n] && value[n] != '\r' && value[n] != '\n' && n + 1 < size) {
        out[n] = value[n];
        n++;
    }
    out[n] = 0;
}

/* Downloads into body. Returns the status, or 0 with page->error set. */
static int http_get(char *error, const char *url, char *location)
{
    static char headers[HEADERS_MAX];
    char host[HOST_MAX], host_header[HOST_MAX], request[WEB_URL_MAX + HOST_MAX + 128];
    const char *path, *colon;
    unsigned int ip, port, host_length;
    int connection, rc, status, header_end = -1;

    use_tls = starts_with(url, "https://");
    port = use_tls ? HTTPS_PORT : HTTP_PORT;
    url += use_tls ? 8 : 7;
    path = strchr(url, '/');
    if (!path)
        path = url + strlen(url);
    host_length = (unsigned int)(path - url);
    if (host_length >= HOST_MAX) {
        set_error(error, "host name too long", 0);
        return 0;
    }
    memcpy(host, url, host_length);
    host[host_length] = 0;
    memcpy(host_header, host, host_length + 1);
    colon = strchr(host, ':');
    if (colon) {
        port = (unsigned int)atoi(colon + 1);
        host[colon - host] = 0;
    }

    rc = resolve_host(host, &ip);
    if (rc < 0) {
        set_error(error, "could not find the server: ", strerror(rc));
        return 0;
    }
    connection = tcp_open(ip, port);
    if (connection < 0) {
        set_error(error, "could not connect: ", strerror(connection));
        return 0;
    }
    if (use_tls && tls_connect(connection, host) < 0) {
        set_error(error, "secure connection failed: ", tls_error());
        tcp_close(connection);
        use_tls = 0;
        return 0;
    }

    copy_text(request, sizeof(request), "GET ");
    copy_text(request + strlen(request), sizeof(request) - strlen(request), *path ? path : "/");
    copy_text(request + strlen(request), sizeof(request) - strlen(request), " HTTP/1.1\r\nHost: ");
    copy_text(request + strlen(request), sizeof(request) - strlen(request), host_header);
    copy_text(request + strlen(request), sizeof(request) - strlen(request),
              "\r\nUser-Agent: " USER_AGENT "\r\nAccept: text/html, text/plain\r\nConnection: close\r\n\r\n");
    rc = send_all(connection, request);
    if (rc < 0) {
        close_transport(connection);
        set_error(error, "could not send the request: ", strerror(rc));
        return 0;
    }

    body_length = 0;
    while (body_length < BODY_MAX - 1) {
        rc = receive_some(connection, body + body_length, (unsigned int)(BODY_MAX - 1 - body_length));
        if (rc <= 0)
            break;
        body_length += rc;
    }
    close_transport(connection);
    if (rc < 0 && body_length == 0) {
        set_error(error, "the server did not answer: ", strerror(rc));
        return 0;
    }

    for (int i = 0; i + 3 < body_length; i++) {
        if (body[i] == '\r' && body[i + 1] == '\n' && body[i + 2] == '\r' && body[i + 3] == '\n') {
            header_end = i + 4;
            break;
        }
    }
    if (header_end < 0 || header_end >= HEADERS_MAX || !starts_with(body, "HTTP/")) {
        set_error(error, "the server sent something that is not HTTP", 0);
        return 0;
    }

    memcpy(headers, body, (unsigned int)header_end);
    headers[header_end] = 0;
    {
        const char *space = strchr(headers, ' ');

        status = space ? atoi(space + 1) : 0;
    }

    body_length -= header_end;
    for (int i = 0; i < body_length; i++)
        body[i] = body[i + header_end];
    body[body_length] = 0;

    {
        const char *value = find_header(headers, "Transfer-Encoding:");

        if (value && starts_with(value, "chunked")) {
            dechunk();
            body[body_length] = 0;
        }
        value = find_header(headers, "Location:");
        if (value)
            header_value(value, location, WEB_URL_MAX);
    }
    return status;
}

static int read_file(char *error, const char *path)
{
    int fd = open(path, BS_O_READ);

    if (fd < 0) {
        set_error(error, "could not open that file: ", strerror(fd));
        return -1;
    }
    body_length = read(fd, body, BODY_MAX - 1);
    close(fd);
    if (body_length < 0) {
        set_error(error, "could not read that file: ", strerror(body_length));
        body_length = 0;
        return -1;
    }
    body[body_length] = 0;
    return 0;
}

/* Turns a possibly relative link into an absolute URL. */
static void resolve_url(const char *base, const char *link, char *out)
{
    unsigned int scheme_end, length;

    while (*link == ' ')
        link++;
    if (starts_with(link, "http://") || starts_with(link, "https://") || starts_with(link, "file:") ||
        starts_with(link, "mailto:") || starts_with(link, "javascript:")) {
        copy_text(out, WEB_URL_MAX, link);
        return;
    }

    {
        unsigned int authority = starts_with(base, "https://") ? 8 : 7;

        scheme_end = starts_with(base, "file:") ? 5 : (unsigned int)(strchr(base + authority, '/') ? strchr(base + authority, '/') - base : (int)strlen(base));
    }
    if (starts_with(link, "//")) {
        unsigned int prefix = starts_with(base, "https://") ? 6 : 5;

        copy_text(out, WEB_URL_MAX, prefix == 6 ? "https:" : "http:");
        copy_text(out + prefix, WEB_URL_MAX - prefix, link);
        return;
    }

    if (link[0] == '/') {
        length = scheme_end;
    } else {
        length = strlen(base);
        while (length > scheme_end && base[length - 1] != '/')
            length--;
        if (length == scheme_end && !starts_with(base, "file:")) {
            if (length >= WEB_URL_MAX - 2)
                length = WEB_URL_MAX - 2;
            memcpy(out, base, length);
            out[length++] = '/';
            copy_text(out + length, WEB_URL_MAX - length, link);
            return;
        }
    }
    if (length >= WEB_URL_MAX)
        length = WEB_URL_MAX - 1;
    memcpy(out, base, length);
    copy_text(out + length, WEB_URL_MAX - length, link);
}

/* ---- Layout ------------------------------------------------------------ */

static struct web_page *target;
static int wrap_width, column;
static char word[WORD_MAX];
static int word_length, word_link, pending_space, current_link;

static void start_line(void)
{
    if (target->line_count >= WEB_LINES_MAX)
        return;
    target->lines[target->line_count].start = target->text_used;
    target->lines[target->line_count].length = 0;
    target->line_count++;
    column = 0;
}

static void put_text(char c)
{
    if (target->text_used >= WEB_TEXT_MAX || target->line_count == 0)
        return;
    target->text[target->text_used++] = c;
    target->lines[target->line_count - 1].length++;
    column++;
}

static void flush_word(void)
{
    if (word_length == 0)
        return;
    if (column > 0 && column + (pending_space ? 1 : 0) + word_length > wrap_width)
        start_line();
    else if (pending_space && column > 0)
        put_text(' ');
    pending_space = 0;

    if (word_link >= 0 && target->span_count < WEB_SPANS_MAX && target->line_count > 0) {
        struct web_span *span = &target->spans[target->span_count++];

        span->line = target->line_count - 1;
        span->column = column;
        span->length = word_length > wrap_width ? wrap_width : word_length;
        span->link = word_link;
    }
    for (int i = 0; i < word_length; i++) {
        if (column >= wrap_width)
            start_line();
        put_text(word[i]);
    }
    word_length = 0;
}

static void add_char(char c)
{
    if (c == ' ' || c == '\n' || c == '\r' || c == '\t') {
        flush_word();
        pending_space = 1;
        return;
    }
    if ((unsigned char)c < 0x20 || (unsigned char)c > 0x7E)
        c = '?';
    if (word_length == WORD_MAX)
        flush_word();
    if (word_length == 0)
        word_link = current_link;
    word[word_length++] = c;
}

static void add_string(const char *s)
{
    for (; *s; s++)
        add_char(*s);
}

static void line_break(void)
{
    flush_word();
    pending_space = 0;
    if (column > 0)
        start_line();
}

static void blank_line(void)
{
    line_break();
    if (target->line_count > 0 && target->lines[target->line_count - 1].length == 0 &&
        target->line_count > 1 && target->lines[target->line_count - 2].length == 0)
        return;
    if (target->line_count > 0)
        start_line();
}

static int tag_is(const char *tag, const char *name)
{
    unsigned int n = strlen(name);

    return starts_with(tag, name) && (tag[n] == 0 || tag[n] == ' ' || tag[n] == '/' || tag[n] == '\n' ||
                                      tag[n] == '\t' || tag[n] == '\r');
}

/* Copies a tag attribute's value into out. Returns nonzero if present. */
static int attribute(const char *tag, const char *name, char *out, unsigned int size)
{
    unsigned int n = strlen(name);

    for (const char *p = tag; *p; p++) {
        unsigned int length = 0;
        char quote;

        if (!starts_with(p, name) || (p > tag && p[-1] != ' ' && p[-1] != '\n' && p[-1] != '\t'))
            continue;
        p += n;
        while (*p == ' ')
            p++;
        if (*p != '=')
            continue;
        p++;
        while (*p == ' ')
            p++;
        quote = *p == '"' || *p == '\'' ? *p++ : 0;
        while (*p && (quote ? *p != quote : *p != ' ' && *p != '>') && length + 1 < size)
            out[length++] = *p++;
        out[length] = 0;
        return 1;
    }
    return 0;
}

static void decode_entities_into(const char *text, char *out, unsigned int size);

static void handle_tag(const char *tag)
{
    int closing = tag[0] == '/';
    const char *name = closing ? tag + 1 : tag;

    if (tag_is(name, "br") || tag_is(name, "tr")) {
        line_break();
    } else if (tag_is(name, "p") || tag_is(name, "h1") || tag_is(name, "h2") || tag_is(name, "h3") ||
               tag_is(name, "h4") || tag_is(name, "h5") || tag_is(name, "h6") || tag_is(name, "ul") ||
               tag_is(name, "ol") || tag_is(name, "table") || tag_is(name, "pre") ||
               tag_is(name, "blockquote")) {
        blank_line();
    } else if (tag_is(name, "div") || tag_is(name, "section") || tag_is(name, "article") ||
               tag_is(name, "header") || tag_is(name, "footer") || tag_is(name, "nav") ||
               tag_is(name, "title") || tag_is(name, "dt") || tag_is(name, "dd")) {
        line_break();
    } else if (tag_is(name, "li")) {
        line_break();
        if (!closing)
            add_string("* ");
    } else if (tag_is(name, "td") || tag_is(name, "th")) {
        if (closing)
            add_char(' ');
    } else if (tag_is(name, "hr")) {
        line_break();
        add_string("----------------");
        line_break();
    } else if (tag_is(name, "a")) {
        if (closing && current_link >= 0) {
            char label[16];
            int n = current_link + 1, used = 0;
            char digits[8];
            int count = 0;

            flush_word();
            current_link = -1;
            do {
                digits[count++] = (char)('0' + n % 10);
                n /= 10;
            } while (n);
            label[used++] = '[';
            while (count)
                label[used++] = digits[--count];
            label[used++] = ']';
            label[used] = 0;
            add_string(label);
            flush_word();
        } else if (!closing) {
            char href[WEB_URL_MAX], decoded[WEB_URL_MAX];

            flush_word();
            if (attribute(name, "href", href, sizeof(href)) && target->link_count < WEB_LINKS_MAX) {
                decode_entities_into(href, decoded, sizeof(decoded));
                resolve_url(target->url, decoded, target->links[target->link_count]);
                current_link = target->link_count++;
            }
        }
    }
}

/* The font is ASCII, so other characters become their nearest relative. */
static char closest_ascii(unsigned int c)
{
    static const char latin[] = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTsaaaaaaaceeeeiiiidnooooo/ouuuuyty";

    if (c >= 0x20 && c < 0x7F)
        return (char)c;
    if (c >= 0xC0 && c <= 0xFF)
        return latin[c - 0xC0];
    switch (c) {
    case 0xA0: case 0x2002: case 0x2003: case 0x2009: case 0x200A: case 0x202F: return ' ';
    case 0x2018: case 0x2019: case 0x201A: case 0x2032: return '\'';
    case 0x201C: case 0x201D: case 0x201E: case 0x2033: case 0xAB: case 0xBB: return '"';
    case 0x2010: case 0x2011: case 0x2012: case 0x2013: case 0x2014: case 0x2015: case 0x2212: return '-';
    case 0x2026: case 0xB7: return '.';
    case 0x2022: case 0x25CF: case 0x25AA: return '*';
    case 0xA9: return 'c';
    case 0xAE: return 'r';
    case 0x2122: return 't';
    default: return '?';
    }
}

static int decode_entity(const char *text, char *out)
{
    static const struct { const char *name; char value; } entities[] = {
        {"amp;", '&'}, {"lt;", '<'}, {"gt;", '>'}, {"quot;", '"'}, {"apos;", '\''}, {"nbsp;", ' '},
        {"mdash;", '-'}, {"ndash;", '-'}, {"hellip;", '.'}, {"copy;", 'c'},
    };

    if (text[0] == '#') {
        int value = 0, n = 1;

        if (text[1] == 'x' || text[1] == 'X') {
            for (n = 2; n < 10 && text[n] && text[n] != ';'; n++) {
                char c = lower(text[n]);

                value = value * 16 + (c >= 'a' ? c - 'a' + 10 : c - '0');
            }
        } else {
            for (; n < 9 && text[n] >= '0' && text[n] <= '9'; n++)
                value = value * 10 + (text[n] - '0');
        }
        if (text[n] != ';')
            return 0;
        *out = closest_ascii((unsigned int)value);
        return n + 1;
    }
    for (unsigned int i = 0; i < sizeof(entities) / sizeof(entities[0]); i++) {
        if (starts_with(text, entities[i].name)) {
            *out = entities[i].value;
            return (int)strlen(entities[i].name);
        }
    }
    return 0;
}

static void decode_entities_into(const char *text, char *out, unsigned int size)
{
    unsigned int n = 0;

    while (*text && n + 1 < size) {
        char value;
        int used;

        if (*text == '&' && (used = decode_entity(text + 1, &value)) > 0) {
            out[n++] = value;
            text += used + 1;
        } else {
            out[n++] = *text++;
        }
    }
    out[n] = 0;
}

/* Returns the offset just past "</name ...>", or the end of the document. */
static int skip_element(int position, const char *name)
{
    unsigned int n = strlen(name);

    for (; position < body_length; position++) {
        if (body[position] == '<' && body[position + 1] == '/' && starts_with(body + position + 2, name) &&
            (body[position + 2 + (int)n] == '>' || body[position + 2 + (int)n] == ' ')) {
            while (position < body_length && body[position] != '>')
                position++;
            return position + 1;
        }
    }
    return body_length;
}

static void layout_html(void)
{
    int i = 0;

    while (i < body_length) {
        if (body[i] == '<') {
            static char tag[TAG_MAX];
            int n = 0;

            if (starts_with(body + i, "<!--")) {
                for (i += 4; i < body_length && !starts_with(body + i, "-->"); i++)
                    ;
                i += 3;
                continue;
            }

            i++;
            while (i < body_length && body[i] != '>' && n + 1 < TAG_MAX)
                tag[n++] = body[i++];
            while (i < body_length && body[i] != '>')
                i++;
            i++;
            tag[n] = 0;

            if (tag_is(tag, "script")) {
                i = skip_element(i, "script");
            } else if (tag_is(tag, "style")) {
                i = skip_element(i, "style");
            } else if (tag_is(tag, "title")) {
                int start = i, stop = skip_element(i, "title");
                char raw[WEB_TITLE_MAX];
                int length = 0;

                for (int p = start; p < stop && body[p] != '<' && length + 1 < WEB_TITLE_MAX; p++)
                    raw[length++] = body[p] == '\n' ? ' ' : body[p];
                raw[length] = 0;
                decode_entities_into(raw, target->title, WEB_TITLE_MAX);
                i = stop;
            } else {
                handle_tag(tag);
            }
        } else if (body[i] == '&') {
            char value;
            int used = decode_entity(body + i + 1, &value);

            if (used > 0) {
                add_char(value);
                i += used + 1;
            } else {
                add_char('&');
                i++;
            }
        } else {
            char value;

            i += web_decode_character(body + i, body_length - i, &value);
            add_char(value);
        }
    }
    flush_word();
}

static void layout_plain(void)
{
    for (int i = 0; i < body_length; i++) {
        if (body[i] == '\n') {
            flush_word();
            pending_space = 0;
            start_line();
        } else {
            char value;

            i += web_decode_character(body + i, body_length - i, &value) - 1;
            add_char(value);
        }
    }
    flush_word();
}

static void begin_layout(struct web_page *page, int width)
{
    target = page;
    wrap_width = width;
    column = word_length = pending_space = 0;
    word_link = current_link = -1;
    page->title[0] = page->error[0] = 0;
    page->text_used = page->line_count = page->link_count = page->span_count = 0;
    start_line();
}

static int looks_like_html(void)
{
    for (int i = 0; i < body_length && i < 512; i++)
        if (body[i] == '<')
            return 1;
    return 0;
}

static void show_error_page(struct web_page *page, int width)
{
    char message[WEB_ERROR_MAX];

    memcpy(message, page->error, WEB_ERROR_MAX);
    begin_layout(page, width);
    add_string("This page could not be loaded.");
    line_break();
    start_line();
    add_string(message);
    flush_word();
    memcpy(page->error, message, WEB_ERROR_MAX);
}

void web_load(struct web_page *page, const char *url, int width)
{
    char *error = page->error;
    char current[WEB_URL_MAX], location[WEB_URL_MAX];

    copy_text(current, WEB_URL_MAX, url);
    if (strchr(current, '#'))
        current[strchr(current, '#') - current] = 0;
    if (!starts_with(current, "http://") && !starts_with(current, "https://") && !starts_with(current, "file:") &&
        !starts_with(current, "mailto:") && !starts_with(current, "javascript:")) {
        /* a bare host name, as people type them */
        copy_text(location, WEB_URL_MAX, "http://");
        copy_text(location + 7, WEB_URL_MAX - 7, current);
        memcpy(current, location, WEB_URL_MAX);
    }

    copy_text(page->url, WEB_URL_MAX, current);
    page->error[0] = 0;

    for (int redirects = 0;; redirects++) {
        int status;

        if (starts_with(current, "file:")) {
            if (read_file(error, current + 5) < 0)
                break;
            status = 200;
        } else if (starts_with(current, "http://") || starts_with(current, "https://")) {
            location[0] = 0;
            status = http_get(error, current, location);
            if (status == 0)
                break;
            if (status >= 300 && status < 400 && location[0]) {
                char next[WEB_URL_MAX];

                if (redirects == REDIRECT_LIMIT) {
                    set_error(error, "too many redirects", 0);
                    break;
                }
                resolve_url(current, location, next);
                memcpy(current, next, WEB_URL_MAX);
                copy_text(page->url, WEB_URL_MAX, current);
                continue;
            }
            if (status >= 400) {
                char code[12] = {0};
                int value = status, n = 11;

                do {
                    code[--n] = (char)('0' + value % 10);
                    value /= 10;
                } while (value);
                set_error(error, "the server said error ", code + n);
                break;
            }
        } else {
            set_error(error, "BShitOS does not know this kind of link", 0);
            break;
        }

        begin_layout(page, width);
        if (looks_like_html())
            layout_html();
        else
            layout_plain();
        if (!page->title[0])
            copy_text(page->title, WEB_TITLE_MAX, page->url);
        return;
    }

    show_error_page(page, width);
}

void web_home(char *out, unsigned int size)
{
    if (conf_get(CONF_PATH, "home", out, size) < 0)
        copy_text(out, size, DEFAULT_HOME);
}

/* Decodes one UTF-8 character or entity. Returns the bytes used. */
int web_decode_character(const char *text, int available, char *out)
{
    unsigned char first = (unsigned char)text[0];
    unsigned int value;
    int extra;

    if (first == '&' && available > 1) {
        char decoded;
        int used = decode_entity(text + 1, &decoded);

        if (used > 0) {
            *out = decoded;
            return used + 1;
        }
    }
    if (first < 0x80) {
        *out = first < 0x20 ? ' ' : (char)first;
        return 1;
    }

    if ((first & 0xE0) == 0xC0) {
        value = first & 0x1F;
        extra = 1;
    } else if ((first & 0xF0) == 0xE0) {
        value = first & 0x0F;
        extra = 2;
    } else if ((first & 0xF8) == 0xF0) {
        value = first & 0x07;
        extra = 3;
    } else {
        *out = '?';
        return 1;
    }
    if (extra >= available) {
        *out = '?';
        return available;
    }
    for (int i = 1; i <= extra; i++) {
        if (((unsigned char)text[i] & 0xC0) != 0x80) {
            *out = '?';
            return i;
        }
        value = value << 6 | ((unsigned char)text[i] & 0x3F);
    }
    *out = closest_ascii(value);
    return extra + 1;
}

void web_resolve_url(const char *base, const char *link, char *out)
{
    resolve_url(base, link, out);
}

void web_body(const char **data, int *length)
{
    *data = body;
    *length = body_length;
}

int web_is_html(void)
{
    return looks_like_html();
}

/* Downloads a stylesheet or image into the shared body buffer. */
int web_fetch(const char *url, const char **data, int *length)
{
    char current[WEB_URL_MAX], location[WEB_URL_MAX], error[WEB_ERROR_MAX];

    copy_text(current, WEB_URL_MAX, url);
    for (int redirects = 0; redirects <= REDIRECT_LIMIT; redirects++) {
        int status;

        if (starts_with(current, "file:")) {
            if (read_file(error, current + 5) < 0)
                return -1;
            status = 200;
        } else if (starts_with(current, "http://") || starts_with(current, "https://")) {
            location[0] = 0;
            status = http_get(error, current, location);
            if (status == 0)
                return -1;
            if (status >= 300 && status < 400 && location[0]) {
                char next[WEB_URL_MAX];

                resolve_url(current, location, next);
                memcpy(current, next, WEB_URL_MAX);
                continue;
            }
            if (status >= 400)
                return -1;
        } else {
            return -1;
        }
        *data = body;
        *length = body_length;
        return 0;
    }
    return -1;
}
