#ifndef BSBOOT_MENU_H
#define BSBOOT_MENU_H

#include "efi/efi.h"

struct menu_entry {
    const CHAR16 *label;
    void (*action)(void); /* a returning action comes back to the menu */
};

/* Runs the menu forever. */
void menu_run(const struct menu_entry *entries, UINTN count);

#endif
