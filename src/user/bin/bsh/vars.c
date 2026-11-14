/* Shell variables (NAME=value, $NAME) and aliases. */
#include "bsh.h"

#define VARS_MAX 48
#define VAR_NAME 32
#define VAR_VALUE 256
#define ALIASES_MAX 32
#define ALIAS_VALUE 160

struct entry {
    char name[VAR_NAME];
    char value[VAR_VALUE];
};

static struct entry vars[VARS_MAX];
static struct entry aliases[ALIASES_MAX];

int valid_name(const char *s, unsigned int length)
{
    if (length == 0 || length >= VAR_NAME)
        return 0;
    for (unsigned int i = 0; i < length; i++)
        if (!is_name_char(s[i], i == 0))
            return 0;
    return 1;
}

static struct entry *find(struct entry *table, int count, const char *name)
{
    for (int i = 0; i < count; i++)
        if (table[i].name[0] && strcmp(table[i].name, name) == 0)
            return &table[i];
    return 0;
}

static int store(struct entry *table, int count, const char *name, const char *value, unsigned int value_size)
{
    struct entry *e = find(table, count, name);

    if (strlen(value) >= value_size)
        return -1;
    for (int i = 0; !e && i < count; i++)
        if (!table[i].name[0])
            e = &table[i];
    if (!e)
        return -1;
    copy_string(e->name, name, VAR_NAME);
    copy_string(e->value, value, value_size);
    return 0;
}

/* Sorted output, so `set` and `alias` are easy to scan. */
static void list(struct entry *table, int count, const char *color, const char *quote)
{
    const char *last = "";

    for (;;) {
        struct entry *next = 0;

        for (int i = 0; i < count; i++)
            if (table[i].name[0] && strcmp(table[i].name, last) > 0 &&
                (!next || strcmp(table[i].name, next->name) < 0))
                next = &table[i];
        if (!next)
            return;
        printf("%s%s" C_RESET "=%s%s%s\n", color, next->name, quote, next->value, quote);
        last = next->name;
    }
}

const char *var_get(const char *name)
{
    struct entry *e = find(vars, VARS_MAX, name);

    return e ? e->value : 0;
}

int var_set(const char *name, const char *value)
{
    return store(vars, VARS_MAX, name, value, VAR_VALUE);
}

void var_unset(const char *name)
{
    struct entry *e = find(vars, VARS_MAX, name);

    if (e)
        e->name[0] = 0;
}

void var_list(void)
{
    list(vars, VARS_MAX, C_VAR, "");
}

const char *alias_get(const char *name)
{
    struct entry *e = find(aliases, ALIASES_MAX, name);

    return e ? e->value : 0;
}

int alias_set(const char *name, const char *value)
{
    return store(aliases, ALIASES_MAX, name, value, ALIAS_VALUE);
}

int alias_unset(const char *name)
{
    struct entry *e = find(aliases, ALIASES_MAX, name);

    if (!e)
        return -1;
    e->name[0] = 0;
    return 0;
}

void alias_list(void)
{
    list(aliases, ALIASES_MAX, C_VAR, "'");
}

const char *alias_name(int i)
{
    for (int n = 0; n < ALIASES_MAX; n++)
        if (aliases[n].name[0] && i-- == 0)
            return aliases[n].name;
    return 0;
}
