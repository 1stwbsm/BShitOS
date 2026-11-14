/* useradd name: a new account with its own group and a private home. Root only. */
#include "bs.h"

#define FIRST_UID 1000u
#define HOMES "/userspace/"

static int valid_name(const char *name)
{
    unsigned int n = strlen(name);

    if (n == 0 || n >= BS_USER_NAME_MAX || name[0] == '-')
        return 0;
    for (unsigned int i = 0; i < n; i++)
        if (!((name[i] >= 'a' && name[i] <= 'z') || (name[i] >= '0' && name[i] <= '9') || name[i] == '_' ||
              name[i] == '-'))
            return 0;
    return 1;
}

/* The next free id at or above FIRST_UID, checking both tables. */
static unsigned int next_id(void)
{
    unsigned int id = FIRST_UID;
    char name[BS_USER_NAME_MAX];

    while (user_name(id, name, sizeof(name)) == 0 || group_name(id, name, sizeof(name)) == 0)
        id++;
    return id;
}

static void append_number(char *line, unsigned int value)
{
    char digits[12];
    int n = 0;
    unsigned int used = strlen(line);

    do {
        digits[n++] = (char)('0' + value % 10);
        value /= 10;
    } while (value);
    while (n)
        line[used++] = digits[--n];
    line[used] = 0;
}

static void append(char *line, const char *text)
{
    unsigned int used = strlen(line), n = strlen(text);

    memcpy(line + used, text, n + 1);
}

int main(int argc, char **argv)
{
    char line[BS_PATH_MAX * 2 + 64], home[BS_PATH_MAX], password[128], again[128];
    struct bs_user me, existing;
    unsigned int id;
    int rc;

    if (argc != 2 || !valid_name(argv[1])) {
        printf("usage: useradd name   (a-z 0-9 _ -, under 32 characters)\n");
        return 2;
    }
    whoami(&me);
    if (me.uid != BS_UID_ROOT) {
        printf("\033[91museradd: root only (try: su -c 'useradd %s')\033[0m\n", argv[1]);
        return 1;
    }
    if (user_by_name(argv[1], &existing) == 0) {
        printf("\033[91museradd: %s already exists\033[0m\n", argv[1]);
        return 1;
    }
    if (read_secret("password: ", password, sizeof(password)) < 0 || read_secret("again: ", again, sizeof(again)) < 0)
        return 1;
    if (strcmp(password, again) != 0) {
        printf("\033[91museradd: passwords don't match, nothing created\033[0m\n");
        return 1;
    }

    id = next_id();
    home[0] = 0;
    append(home, HOMES);
    append(home, argv[1]);

    line[0] = 0; /* group first, then the account */
    append(line, argv[1]);
    append(line, ":");
    append_number(line, id);
    append(line, ":");
    rc = table_append("/conf/group", line);
    if (rc == 0) {
        line[0] = 0;
        append(line, argv[1]);
        append(line, ":");
        append_number(line, id);
        append(line, ":");
        append_number(line, id);
        append(line, ":");
        append(line, home);
        append(line, ":/bin/bsh.bs");
        rc = table_append("/conf/passwd", line);
    }
    if (rc == 0)
        rc = set_password(argv[1], "", password);
    memset(password, 0, sizeof(password));
    memset(again, 0, sizeof(again));
    if (rc < 0) {
        printf("\033[91museradd: %s\033[0m\n", strerror(rc));
        return 1;
    }

    rc = mkdir(home);
    if (rc < 0 && rc != -BS_EEXIST)
        printf("\033[93museradd: %s: %s\033[0m\n", home, strerror(rc));
    chown(home, id, id);
    chmod(home, 0700);
    printf("%s: uid %u, home %s\n", argv[1], id, home);
    return 0;
}
