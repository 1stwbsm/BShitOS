/* poweroff: asks every program to exit, then the kernel flushes the disks and powers off. */
#include "bs.h"

int main(void)
{
    if (shutdown(BS_SHUTDOWN_POWEROFF) < 0) {
        printf("\033[91mpoweroff: could not start a shutdown\033[0m\n");
        return 1;
    }
    printf("\033[90mshutting down: programs get 15 s to exit\033[0m\n");
    return 0;
}
