/*
 * chmod MODE path...: MODE is octal (755) or symbolic, comma-separated:
 * [ugoa]*[+-=][rwx]*, e.g. +x, go-w, u=rw,go=r.
 */
#include "bs.h"

/* Applies MODE to perm. Returns -1 if MODE makes no sense. */
static int apply(const char *mode, unsigned int perm, unsigned int *out)
{
    if (mode[0] >= '0' && mode[0] <= '7') {
        unsigned int value = 0;

        for (const char *p = mode; *p; p++) {
            if (*p < '0' || *p > '7' || p - mode >= 4)
                return -1;
            value = value * 8 + (unsigned int)(*p - '0');
        }
        *out = value & 0777;
        return 0;
    }
    for (const char *p = mode; *p;) {
        unsigned int who = 0, bits = 0, mask;
        char op;

        for (; *p == 'u' || *p == 'g' || *p == 'o' || *p == 'a'; p++)
            who |= *p == 'u' ? 0700 : *p == 'g' ? 0070 : *p == 'o' ? 0007 : 0777;
        if (who == 0)
            who = 0777;
        op = *p++;
        if (op != '+' && op != '-' && op != '=')
            return -1;
        for (; *p == 'r' || *p == 'w' || *p == 'x'; p++)
            bits |= *p == 'r' ? 0444 : *p == 'w' ? 0222 : 0111;
        mask = bits & who;
        if (op == '+')
            perm |= mask;
        else if (op == '-')
            perm &= ~mask;
        else
            perm = (perm & ~who) | mask;
        if (*p == ',')
            p++;
        else if (*p)
            return -1;
    }
    *out = perm & 0777;
    return 0;
}

int main(int argc, char **argv)
{
    int status = 0;

    if (argc < 3) {
        printf("usage: chmod MODE path...   (755, +x, go-w, u=rw,go=r)\n");
        return 2;
    }
    for (int i = 2; i < argc; i++) {
        struct bs_stat st;
        unsigned int perm;
        int rc = stat(argv[i], &st);

        if (rc == 0 && apply(argv[1], st.perm, &perm) < 0) {
            printf("\033[91mchmod: %s: not a mode\033[0m\n", argv[1]);
            return 2;
        }
        if (rc == 0)
            rc = chmod(argv[i], perm);
        if (rc < 0) {
            printf("\033[91mchmod: %s: %s\033[0m\n", argv[i], rc == -BS_EPERM ? "not yours" : strerror(rc));
            status = 1;
        }
    }
    return status;
}
