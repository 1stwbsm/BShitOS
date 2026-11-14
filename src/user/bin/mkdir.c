#include "bs.h"

int main(int argc, char **argv)
{
    int status = 0;

    if (argc < 2) {
        printf("usage: mkdir dir...\n");
        return 1;
    }
    for (int i = 1; i < argc; i++) {
        int rc = mkdir(argv[i]);

        if (rc < 0) {
            printf("mkdir: %s: %s\n", argv[i], strerror(rc));
            status = 1;
        }
    }
    return status;
}
