#include "bs.h"

static void print_ip(unsigned int ip)
{
    printf("%u.%u.%u.%u", ip >> 24, (ip >> 16) & 255, (ip >> 8) & 255, ip & 255);
}

int main(void)
{
    struct bs_netinfo info;

    net_info(&info);
    if (!info.present) {
        printf("no network card found\n");
        return 1;
    }
    printf("mac      ");
    for (int i = 0; i < 6; i++)
        printf("%x%x%s", (unsigned int)info.mac[i] >> 4, info.mac[i] & 15u, i < 5 ? ":" : "\n");
    if (!info.ip) {
        printf("no address yet (DHCP did not answer)\n");
        return 1;
    }
    printf("address  ");
    print_ip(info.ip);
    printf("\nnetmask  ");
    print_ip(info.mask);
    printf("\ngateway  ");
    print_ip(info.gateway);
    printf("\ndns      ");
    print_ip(info.dns);
    printf("\n");
    return 0;
}
