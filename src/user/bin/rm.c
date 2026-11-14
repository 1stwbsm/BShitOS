#include "bs.h"

int main(int argc, char **argv)
{
    int status = 0;

    if (argc < 2) {
        printf("usage: rm path...\n");
        return 1;
    }
    for (int i = 1; i < argc; i++) {
        int rc = unlink(argv[i]);

        if (rc < 0) {
            printf("rm: %s: %s\n", argv[i], strerror(rc));
            status = 1;
        }
    }
    return status;
}
