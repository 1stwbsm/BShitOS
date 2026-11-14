/* libbs's web engine: fetches a page and lays it out as wrapped text. */
#ifndef WEB_H
#define WEB_H

#define WEB_URL_MAX 256
#define WEB_TITLE_MAX 80
#define WEB_ERROR_MAX 96
#define WEB_TEXT_MAX (192 * 1024)
#define WEB_LINES_MAX 6000
#define WEB_LINKS_MAX 400
#define WEB_SPANS_MAX 2000

struct web_line {
    int start, length;
};

/* A run of text on one line that belongs to a link. */
struct web_span {
    int line, column, length, link;
};

struct web_page {
    char url[WEB_URL_MAX];
    char title[WEB_TITLE_MAX];
    char error[WEB_ERROR_MAX]; /* empty if the page loaded */
    char text[WEB_TEXT_MAX];
    int text_used;
    struct web_line lines[WEB_LINES_MAX];
    int line_count;
    char links[WEB_LINKS_MAX][WEB_URL_MAX];
    int link_count;
    struct web_span spans[WEB_SPANS_MAX];
    int span_count;
};

/* Loads url (http: or file:) into page, wrapping text at width columns. */
void web_load(struct web_page *page, const char *url, int width);
/* Reads the home page from /conf/web.conf into out. */
void web_home(char *out, unsigned int size);

/* For the page engine. */
void web_resolve_url(const char *base, const char *link, char *out);
void web_body(const char **data, int *length);
int web_is_html(void);
int web_fetch(const char *url, const char **data, int *length);
int web_decode_character(const char *text, int available, char *out);

#endif
