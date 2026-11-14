/* edit: a full-screen text editor. Ctrl+S saves, Ctrl+Q quits. */
#include "bs.h"

#define CONF_PATH "/conf/edit.conf"
#define MAX_TEXT (128 * 1024)
#define OUTPUT_MAX 16384
#define MESSAGE_MAX 64
#define CTRL(c) ((c) & 0x1F)
#define DEFAULT_TAB_WIDTH 4
#define MAX_TAB_WIDTH 16

static char text[MAX_TEXT];
static int length, caret, top_line, left_column;
static int rows, cols, tab_width = DEFAULT_TAB_WIDTH;
static int modified, quit_armed;
static const char *path;
static char message[MESSAGE_MAX];

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

static void emit_char(char c)
{
    char s[2] = {c, 0};

    emit(s);
}

static void emit_number(int value)
{
    char reversed[12];
    int count = 0;

    do {
        reversed[count++] = (char)('0' + value % 10);
        value /= 10;
    } while (value);
    while (count)
        emit_char(reversed[--count]);
}

static void move_to(int row, int column)
{
    emit("\033[");
    emit_number(row + 1);
    emit(";");
    emit_number(column + 1);
    emit("H");
}

static void set_message(const char *s)
{
    unsigned int n = strlen(s);

    if (n >= MESSAGE_MAX)
        n = MESSAGE_MAX - 1;
    memcpy(message, s, n);
    message[n] = 0;
}

static int line_start(int position)
{
    while (position > 0 && text[position - 1] != '\n')
        position--;
    return position;
}

static int line_end(int position)
{
    while (position < length && text[position] != '\n')
        position++;
    return position;
}

static int caret_line(void)
{
    int line = 0;

    for (int i = 0; i < caret; i++)
        if (text[i] == '\n')
            line++;
    return line;
}

static void scroll_to_caret(void)
{
    int line = caret_line(), column = caret - line_start(caret);

    if (line < top_line)
        top_line = line;
    if (line >= top_line + rows - 1)
        top_line = line - rows + 2;
    if (column < left_column)
        left_column = column;
    if (column >= left_column + cols - 1)
        left_column = column - cols + 2;
}

static int digits(int value)
{
    int count = 1;

    while (value >= 10) {
        value /= 10;
        count++;
    }
    return count;
}

static void draw_status(int line, int column)
{
    int used = 0;

    move_to(rows - 1, 0);
    emit("\033[7m");

    for (const char *p = path; *p && used < cols / 2; p++, used++)
        emit_char(*p);
    if (modified && used < cols - 2) {
        emit(" *");
        used += 2;
    }

    emit("  ln ");
    emit_number(line + 1);
    emit(" col ");
    emit_number(column + 1);
    used += 5 + digits(line + 1) + 5 + digits(column + 1);

    {
        const char *right = message[0] ? message : "^S save  ^Q quit";
        int right_length = (int)strlen(right);

        while (used < cols - 1 - right_length) {
            emit(" ");
            used++;
        }
        if (used + right_length < cols)
            emit(right);
    }
    emit("\033[0m\033[K");
}

static void render(void)
{
    int position = 0, line = 0;

    scroll_to_caret();

    /* Skip to the first visible line. */
    while (line < top_line && position < length) {
        position = line_end(position) + 1;
        line++;
    }

    for (int row = 0; row < rows - 1; row++) {
        move_to(row, 0);
        if (position <= length) {
            int end = line_end(position);
            int start = position + left_column;

            for (int i = start, shown = 0; i < end && shown < cols - 1; i++, shown++)
                emit_char(text[i] >= 0x20 && text[i] < 0x7F ? text[i] : '?');
            position = end + 1;
        } else {
            emit("~");
        }
        emit("\033[K");
    }

    draw_status(caret_line(), caret - line_start(caret));
    move_to(caret_line() - top_line, caret - line_start(caret) - left_column);
    flush();
}

static void insert(char c)
{
    if (length >= MAX_TEXT - 1) {
        set_message("file is full");
        return;
    }
    for (int i = length; i > caret; i--)
        text[i] = text[i - 1];
    text[caret++] = c;
    length++;
    modified = 1;
}

static void delete_at(int position)
{
    for (int i = position; i < length - 1; i++)
        text[i] = text[i + 1];
    length--;
    modified = 1;
}

static void move_vertical(int lines)
{
    int column = caret - line_start(caret);

    for (; lines < 0; lines++) {
        int start = line_start(caret);

        if (start == 0)
            break;
        caret = line_start(start - 1);
    }
    for (; lines > 0; lines--) {
        int end = line_end(caret);

        if (end >= length)
            break;
        caret = end + 1;
    }

    {
        int start = line_start(caret);

        caret = start + column;
        if (caret > line_end(start))
            caret = line_end(start);
    }
}

static void save(void)
{
    int fd = open(path, BS_O_WRITE), written;

    if (fd < 0) {
        set_message(strerror(fd));
        return;
    }
    written = write(fd, text, (unsigned int)length);
    close(fd);
    if (written == length) {
        modified = 0;
        set_message("saved");
    } else {
        set_message(written < 0 ? strerror(written) : "disk full, file is incomplete");
    }
}

static int load(void)
{
    int fd = open(path, BS_O_READ), got;
    char extra;

    if (fd == -BS_ENOENT) {
        set_message("new file");
        return 0;
    }
    if (fd < 0) {
        printf("edit: %s: %s\n", path, strerror(fd));
        return -1;
    }

    length = read(fd, text, MAX_TEXT - 1);
    if (length < 0) {
        printf("edit: %s: %s\n", path, strerror(length));
        close(fd);
        return -1;
    }
    got = read(fd, &extra, 1);
    close(fd);
    if (got > 0) {
        printf("edit: %s: too large to edit\n", path);
        return -1;
    }
    return 0;
}

/* Returns nonzero when the editor should close. */
static int handle_key(int key)
{
    int was_armed = quit_armed;

    quit_armed = 0;
    message[0] = 0;

    switch (key) {
    case CTRL('q'):
        if (modified && !was_armed) {
            quit_armed = 1;
            set_message("unsaved changes: ^Q again to discard");
            return 0;
        }
        return 1;
    case CTRL('s'):
        save();
        break;
    case '\b':
        if (caret > 0)
            delete_at(--caret);
        break;
    case BS_KEY_DEL:
        if (caret < length)
            delete_at(caret);
        break;
    case BS_KEY_LEFT:
        if (caret > 0)
            caret--;
        break;
    case BS_KEY_RIGHT:
        if (caret < length)
            caret++;
        break;
    case BS_KEY_UP:
        move_vertical(-1);
        break;
    case BS_KEY_DOWN:
        move_vertical(1);
        break;
    case BS_KEY_PGUP:
        move_vertical(-(rows - 2));
        break;
    case BS_KEY_PGDN:
        move_vertical(rows - 2);
        break;
    case BS_KEY_HOME:
        caret = line_start(caret);
        break;
    case BS_KEY_END:
        caret = line_end(caret);
        break;
    case '\n':
        insert('\n');
        break;
    case '\t':
        for (int i = 0; i < tab_width; i++)
            insert(' ');
        break;
    default:
        if (key >= 0x20 && key < 0x7F)
            insert((char)key);
        break;
    }
    return 0;
}

int main(int argc, char **argv)
{
    char value[8];

    if (argc != 2) {
        printf("usage: edit file\n");
        return 1;
    }
    path = argv[1];
    if (load() < 0)
        return 1;

    if (conf_get(CONF_PATH, "tab_width", value, sizeof(value)) == 0) {
        int width = atoi(value);

        if (width >= 1 && width <= MAX_TAB_WIDTH)
            tab_width = width;
    }

    console_size(&cols, &rows);
    console_clear();
    render();

    for (;;) {
        int key = poll_key(), closing = 0;

        if (!key) {
            sleep_ms(5);
            continue;
        }

        /* Handle everything queued before paying for one redraw. */
        while (key && !closing) {
            closing = handle_key(key);
            if (!closing)
                key = poll_key();
        }
        if (closing)
            break;
        render();
    }

    console_clear();
    return 0;
}
