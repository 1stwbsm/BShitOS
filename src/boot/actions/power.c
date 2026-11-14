#include "actions/actions.h"
#include "lib/lib.h"

static void reset(EFI_RESET_TYPE type, const CHAR16 *failure)
{
    gST->RuntimeServices->ResetSystem(type, EFI_SUCCESS, 0, 0);
    /* ResetSystem does not return when it works. */
    con_error(failure, 0, EFI_SUCCESS);
}

void action_power_off(void)
{
    reset(EfiResetShutdown, L"The firmware could not power off");
}

void action_reboot(void)
{
    reset(EfiResetCold, L"The firmware could not reboot");
}
