#include "bs.h"

int main(int argc, char **argv)
{
    char buf[1024];
    int in, out, got;

    if (argc != 3) {
        printf("usage: cp from to\n");
        return 1;
    }
    in = open(argv[1], BS_O_READ);
    if (in < 0) {
        printf("cp: %s: %s\n", argv[1], strerror(in));
        return 1;
    }
    out = open(argv[2], BS_O_WRITE);
    if (out < 0) {
        printf("cp: %s: %s\n", argv[2], strerror(out));
        close(in);
        return 1;
    }
    while ((got = read(in, buf, sizeof(buf))) > 0) {
        int written = write(out, buf, (unsigned int)got);

        if (written != got) {
            printf("cp: %s: %s\n", argv[2], strerror(written < 0 ? written : BS_ENOSPC));
            close(in);
            close(out);
            return 1;
        }
    }
    close(in);
    close(out);
    if (got < 0) {
        printf("cp: %s: %s\n", argv[1], strerror(got));
        return 1;
    }
    return 0;
}
