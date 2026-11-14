#include "bs.h"

int main(int argc, char **argv)
{
    char buf[1024];
    int status = 0;

    if (argc < 2) {
        printf("usage: cat file...\n");
        return 1;
    }
    for (int i = 1; i < argc; i++) {
        int fd = open(argv[i], BS_O_READ), got;

        if (fd < 0) {
            printf("cat: %s: %s\n", argv[i], strerror(fd));
            status = 1;
            continue;
        }
        while ((got = read(fd, buf, sizeof(buf))) > 0)
            write(1, buf, (unsigned int)got);
        if (got < 0) {
            printf("cat: %s: %s\n", argv[i], strerror(got));
            status = 1;
        }
        close(fd);
    }
    return status;
}
