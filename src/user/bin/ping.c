#include "bs.h"

#define COUNT 4
#define TIMEOUT_MS 2000

int main(int argc, char **argv)
{
    unsigned int ip;
    int rc;

    if (argc != 2) {
        printf("usage: ping host\n");
        return 1;
    }
    rc = dns_lookup(argv[1], &ip);
    if (rc < 0) {
        printf("ping: %s: %s\n", argv[1], strerror(rc));
        return 1;
    }
    for (int i = 0; i < COUNT; i++) {
        rc = ping(ip, TIMEOUT_MS);
        if (rc < 0)
            printf("no reply: %s\n", strerror(rc));
        else
            printf("reply from %u.%u.%u.%u in %d ms\n", ip >> 24, (ip >> 16) & 255, (ip >> 8) & 255, ip & 255, rc);
        sleep_ms(500);
    }
    return 0;
}
