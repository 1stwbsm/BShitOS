/* write file text...: replaces file with the given words and a newline. */
#include "bs.h"

int main(int argc, char **argv)
{
    int fd;

    if (argc < 2) {
        printf("usage: write file text...\n");
        return 1;
    }
    fd = open(argv[1], BS_O_WRITE);
    if (fd < 0) {
        printf("write: %s: %s\n", argv[1], strerror(fd));
        return 1;
    }
    for (int i = 2; i < argc; i++) {
        int rc;

        if (i > 2)
            write(fd, " ", 1);
        rc = write(fd, argv[i], strlen(argv[i]));
        if (rc < 0) {
            printf("write: %s: %s\n", argv[1], strerror(rc));
            close(fd);
            return 1;
        }
    }
    write(fd, "\n", 1);
    close(fd);
    return 0;
}
