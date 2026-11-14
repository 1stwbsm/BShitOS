/* whoami: the current user's name. */
#include "bs.h"

int main(void)
{
    struct bs_user me;

    whoami(&me);
    printf("%s\n", me.name);
    return 0;
}
