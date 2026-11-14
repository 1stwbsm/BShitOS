#include "bs.h"

#define CONF_MAX 2048

static char data[CONF_MAX];

static int load(const char *path)
{
    int fd = open(path, BS_O_READ), got;

    data[0] = 0;
    if (fd < 0)
        return fd;
    got = read(fd, data, CONF_MAX - 1);
    close(fd);
    if (got < 0)
        return got;
    data[got] = 0;
    return got;
}

/* Finds the line holding key; returns it or 0, and sets *end to its end. */
static char *find_line(const char *key, char **end)
{
    unsigned int key_length = strlen(key);

    for (char *line = data; *line;) {
        char *stop = strchr(line, '\n');

        if (!stop)
            stop = line + strlen(line);
        if (strncmp(line, key, key_length) == 0 && line[key_length] == '=') {
            *end = stop;
            return line;
        }
        line = *stop ? stop + 1 : stop;
    }
    return 0;
}

int conf_get(const char *path, const char *key, char *out, unsigned int size)
{
    char *line, *end;
    unsigned int key_length = strlen(key), length;
    int rc = load(path);

    if (rc < 0)
        return rc;
    line = find_line(key, &end);
    if (!line)
        return -BS_ENOENT;
    length = (unsigned int)(end - line) - key_length - 1;
    if (length >= size)
        return -BS_ENAMETOOLONG;
    memcpy(out, line + key_length + 1, length);
    out[length] = 0;
    return 0;
}

static int write_all(int fd, const char *text, unsigned int length)
{
    return write(fd, text, length) == (int)length ? 0 : -BS_EIO;
}

/* Replaces or appends one setting, leaving every other line alone. */
int conf_set(const char *path, const char *key, const char *value)
{
    char *line, *end;
    unsigned int before, after_start;
    int rc = load(path), fd;

    if (rc < 0 && rc != -BS_ENOENT)
        return rc;
    line = find_line(key, &end);
    before = line ? (unsigned int)(line - data) : strlen(data);
    after_start = line ? (unsigned int)(end - data) + (*end ? 1 : 0) : before;

    fd = open(path, BS_O_WRITE);
    if (fd < 0)
        return fd;
    rc = write_all(fd, data, before);
    if (rc == 0)
        rc = write_all(fd, key, strlen(key));
    if (rc == 0)
        rc = write_all(fd, "=", 1);
    if (rc == 0)
        rc = write_all(fd, value, strlen(value));
    if (rc == 0)
        rc = write_all(fd, "\n", 1);
    if (rc == 0)
        rc = write_all(fd, data + after_start, strlen(data + after_start));
    close(fd);
    return rc;
}
