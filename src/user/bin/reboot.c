/* reboot: asks every program to exit, then the kernel flushes the disks and restarts. */
#include "bs.h"

int main(void)
{
    if (shutdown(BS_SHUTDOWN_REBOOT) < 0) {
        printf("\033[91mreboot: could not start a restart\033[0m\n");
        return 1;
    }
    printf("\033[90mrestarting: programs get 15 s to exit\033[0m\n");
    return 0;
}
