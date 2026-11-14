/* Everything the boot menu can launch. */
#ifndef BSBOOT_ACTIONS_H
#define BSBOOT_ACTIONS_H

void action_boot_kernel(void);
void action_efi_shell(void);
void action_play_dnb(void);
void action_power_off(void);
void action_reboot(void);

#endif
