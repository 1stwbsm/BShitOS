#include "actions/actions.h"
#include "lib/lib.h"
#include "ui/menu.h"

EFI_HANDLE gImage;
EFI_SYSTEM_TABLE *gST;
EFI_BOOT_SERVICES *gBS;

/* Add a line here (and a file in actions/) to add a boot option. */
static const struct menu_entry menu[] = {
    {L"Boot BShitOS Kernel", action_boot_kernel},
    {L"EFI shell",           action_efi_shell},
    {L"play some sick D&B",  action_play_dnb},
    {L"Power off",           action_power_off},
    {L"Reboot",              action_reboot},
};

EFI_STATUS efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *system_table)
{
    gImage = image;
    gST = system_table;
    gBS = system_table->BootServices;

    /* The firmware would otherwise reset the machine after 5 minutes. */
    gBS->SetWatchdogTimer(0, 0, 0, 0);

    con_detach_serial();

    menu_run(menu, sizeof(menu) / sizeof(menu[0]));
    return EFI_SUCCESS;
}
