#include "bs.h"

int main(int argc, char **argv)
{
    beep(argc > 1 ? (unsigned int)atoi(argv[1]) : 880, argc > 2 ? (unsigned int)atoi(argv[2]) : 200);
    return 0;
}
