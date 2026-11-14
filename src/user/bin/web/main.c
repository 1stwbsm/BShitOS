/* web: the BShitOS web browser. Text by default; P switches to the mouse UI. */
#include "bs.h"
#include "ui.h"

#define HISTORY_MAX 16
#define OUTPUT_MAX 16384
#define INPUT_MAX 256

struct web_page page;
int top;

static char history[HISTORY_MAX][WEB_URL_MAX];
static int history_count;
static int rows, cols, columns;

static char output[OUTPUT_MAX];
static unsigned int output_used;

static void flush(void)
{
    write(1, output, output_used);
    output_used = 0;
}

static void emit(const char *s)
{
    unsigned int n = strlen(s);

    if (output_used + n > OUTPUT_MAX)
        flush();
    memcpy(output + output_used, s, n);
    output_used += n;
}

static void emit_number(int value)
{
    char text[12];
    int n = 11;

    text[n] = 0;
    do {
        text[--n] = (char)('0' + value % 10);
        value /= 10;
    } while (value);
    emit(text + n);
}

static void move_to(int row, int column)
{
    emit("\033[");
    emit_number(row + 1);
    emit(";");
    emit_number(column + 1);
    emit("H");
}

static void clipped(const char *text, int room)
{
    char buffer[2] = {0, 0};

    for (; *text && room > 0; text++, room--) {
        buffer[0] = *text;
        emit(buffer);
    }
}

static void render(void)
{
    char buffer[2] = {0, 0};
    int body_rows = rows - 2;

    move_to(0, 0);
    emit("\033[7m");
    clipped(page.title, cols - 1);
    emit("\033[K\033[0m");

    for (int row = 0; row < body_rows; row++) {
        move_to(row + 1, 0);
        if (top + row < page.line_count) {
            const struct web_line *line = &page.lines[top + row];

            for (int i = 0; i < line->length && i < cols - 1; i++) {
                buffer[0] = page.text[line->start + i];
                emit(buffer);
            }
        }
        emit("\033[K");
    }

    move_to(rows - 1, 0);
    emit("\033[7m");
    clipped("Up/Dn scroll  p:mouse mode  g:go  number:link  b:back  h:home  q:quit", cols - 1);
    emit("\033[K\033[0m");
    flush();
}

static void show_status(const char *text)
{
    move_to(rows - 1, 0);
    emit("\033[7m");
    clipped(text, cols - 1);
    emit("\033[K\033[0m");
    flush();
}

void web_navigate(const char *url, int remember)
{
    show_status("loading...");
    if (remember && page.url[0]) {
        if (history_count == HISTORY_MAX) {
            for (int i = 1; i < HISTORY_MAX; i++)
                memcpy(history[i - 1], history[i], WEB_URL_MAX);
            history_count--;
        }
        memcpy(history[history_count++], page.url, WEB_URL_MAX);
    }
    web_load(&page, url, columns);
    top = 0;
}

void web_back(void)
{
    char url[WEB_URL_MAX];

    if (history_count == 0)
        return;
    memcpy(url, history[--history_count], WEB_URL_MAX);
    web_navigate(url, 0);
}

void web_go_home(void)
{
    char url[WEB_URL_MAX];

    web_home(url, sizeof(url));
    web_navigate(url, 1);
}

/* Reads a line on the bottom row. Returns its length. */
static int prompt(const char *label, char *out, int size)
{
    int got;

    move_to(rows - 1, 0);
    emit("\033[K");
    emit(label);
    flush();
    got = read(0, out, (unsigned int)size - 1);
    if (got <= 0)
        return 0;
    if (out[got - 1] == '\n')
        got--;
    out[got] = 0;
    return got;
}

static void follow_link(int first_digit)
{
    char text[INPUT_MAX], label[16] = "link: ";
    int number;

    label[6] = (char)first_digit;
    label[7] = 0;
    text[0] = (char)first_digit;
    prompt(label, text + 1, INPUT_MAX - 1);
    number = atoi(text);
    if (number >= 1 && number <= page.link_count)
        web_navigate(page.links[number - 1], 1);
}

int main(int argc, char **argv)
{
    char url[WEB_URL_MAX];

    console_size(&cols, &rows);
    columns = cols - 1 < WEB_COLUMNS ? cols - 1 : WEB_COLUMNS;
    console_clear();

    if (argc > 1) {
        unsigned int length = strlen(argv[1]);

        if (length >= WEB_URL_MAX)
            length = WEB_URL_MAX - 1;
        memcpy(url, argv[1], length);
        url[length] = 0;
    } else {
        web_home(url, sizeof(url));
    }

    web_navigate(url, 0);
    render();

    for (;;) {
        int key = poll_key();
        int page_rows = rows - 2;

        if (!key) {
            sleep_ms(5);
            continue;
        }

        if (key == 'q') {
            break;
        } else if (key == 'p' || key == 'P') {
            enum graphical_result result = web_graphical();

            graphics_end();
            if (result == QUIT_BROWSER)
                break;
        } else if (key == BS_KEY_DOWN) {
            if (top + page_rows < page.line_count)
                top++;
        } else if (key == BS_KEY_UP) {
            if (top > 0)
                top--;
        } else if (key == BS_KEY_PGDN || key == ' ') {
            top += page_rows;
            if (top + page_rows > page.line_count)
                top = page.line_count > page_rows ? page.line_count - page_rows : 0;
        } else if (key == BS_KEY_PGUP) {
            top = top > page_rows ? top - page_rows : 0;
        } else if (key == 'g') {
            char typed[WEB_URL_MAX];

            if (prompt("go to: ", typed, sizeof(typed)) > 0)
                web_navigate(typed, 1);
        } else if (key == 'h') {
            web_go_home();
        } else if (key == 'b') {
            web_back();
        } else if (key >= '1' && key <= '9') {
            follow_link(key);
        }
        render();
    }

    console_clear();
    return 0;
}
