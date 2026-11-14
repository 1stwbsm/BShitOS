#include "bs.h"

static void two_digits(unsigned int value)
{
    printf("%u%u", value / 10, value % 10);
}

int main(void)
{
    struct bs_time t;

    get_time(&t);
    printf("%u-", t.year);
    two_digits(t.month);
    printf("-");
    two_digits(t.day);
    printf(" ");
    two_digits(t.hour);
    printf(":");
    two_digits(t.minute);
    printf(":");
    two_digits(t.second);
    printf(" UTC\n");
    return 0;
}
