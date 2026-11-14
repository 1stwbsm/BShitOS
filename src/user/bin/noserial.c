#include "bs.h"

int main(void)
{
    printf("console output stopped going to serial\n");
    set_serial(0);
    return 0;
}
