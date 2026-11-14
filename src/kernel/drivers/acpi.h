/* Just enough ACPI for the power button and soft-off. */
#ifndef KERNEL_ACPI_H
#define KERNEL_ACPI_H

#include <stdint.h>

void acpi_init(uint64_t rsdp);
int acpi_power_available(void);
/* Shuts down if the power button has been pressed. */
void acpi_poll(void);
/* Returns only if the machine could not be powered off. */
void acpi_poweroff(void);

#endif
