#include "conf.h"
#include "fs/vfs.h"
#include "lib/string.h"

#define CONF_MAX 2048

static char data[CONF_MAX];

void kconf_load(const char *path)
{
    struct vnode node;
    int got;

    data[0] = 0;
    if (vfs_open("/", path, &node) < 0 || node.type != BS_TYPE_FILE)
        return;
    got = node.ops->read(&node, 0, data, CONF_MAX - 1);
    data[got > 0 ? got : 0] = 0;
}

int kconf_get(const char *key, char *out, unsigned int size)
{
    unsigned int key_length = strlen(key);

    for (const char *line = data; *line;) {
        const char *end = line;

        while (*end && *end != '\n')
            end++;
        if ((unsigned int)(end - line) > key_length && memcmp(line, key, key_length) == 0 &&
            line[key_length] == '=') {
            unsigned int value_length = (unsigned int)(end - line) - key_length - 1;

            if (value_length >= size)
                return -1;
            memcpy(out, line + key_length + 1, value_length);
            out[value_length] = 0;
            return 0;
        }
        line = *end ? end + 1 : end;
    }
    return -1;
}

uint32_t kconf_hex(const char *text)
{
    uint32_t value = 0;

    for (; *text; text++) {
        uint32_t digit;

        if (*text >= '0' && *text <= '9')
            digit = (uint32_t)(*text - '0');
        else if (*text >= 'a' && *text <= 'f')
            digit = (uint32_t)(*text - 'a' + 10);
        else if (*text >= 'A' && *text <= 'F')
            digit = (uint32_t)(*text - 'A' + 10);
        else
            break;
        value = value * 16 + digit;
    }
    return value;
}
