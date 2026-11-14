/* mv: copies the file, then removes the original. */
#include "bs.h"

int main(int argc, char **argv)
{
    char *cp_argv[3];
    int code, rc;

    if (argc != 3) {
        printf("usage: mv from to\n");
        return 1;
    }
    cp_argv[0] = "cp";
    cp_argv[1] = argv[1];
    cp_argv[2] = argv[2];
    code = spawn("/bin/cp.bs", 3, cp_argv);
    if (code != 0)
        return 1;
    rc = unlink(argv[1]);
    if (rc < 0) {
        printf("mv: %s: %s\n", argv[1], strerror(rc));
        return 1;
    }
    return 0;
}
