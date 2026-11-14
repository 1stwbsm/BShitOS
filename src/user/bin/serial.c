#include "bs.h"

int main(void)
{
    set_serial(1);
    printf("console output now goes to serial too\n");
    return 0;
}
