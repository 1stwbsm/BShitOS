/*
 * Graceful shutdown. Processes run nested, one at a time, so a request only
 * raises a flag: the running program exits, its parent sees the flag when it
 * gets control back and exits too, and so on up to init. Then the kernel
 * flushes the disks and powers off. Programs get a deadline; one still running
 * after it is stopped at its next system call.
 */
#include "drivers/acpi.h"
#include "drivers/console.h"
#include "drivers/rtc.h"
#include "drivers/block.h"
#include "proc/proc.h"
#include "proc/shutdown.h"

#define DEADLINE_SECONDS 15u

static int mode;
static uint32_t deadline;

int shutdown_pending(void)
{
    return mode;
}

void shutdown_request(int requested)
{
    if (mode || (requested != BS_SHUTDOWN_POWEROFF && requested != BS_SHUTDOWN_REBOOT))
        return;
    mode = requested;
    deadline = rtc_seconds() + DEADLINE_SECONDS;
}

void shutdown_check(void)
{
    struct proc *p = proc_current();

    if (!mode || !p || rtc_seconds() <= deadline)
        return;
    console_graphics_end();
    console_print("[\033[93mwarn\033[0m] ");
    console_print(p->name);
    console_print(" did not exit in time; stopping it\n");
    proc_exit(BS_EXIT_STOPPED);
}

void shutdown_finish(void)
{
    console_graphics_end();
    console_print("\n[\033[96minfo\033[0m] every program has exited\n");
    if (block_present() && block_flush() == 0)
        console_print("[\033[92m ok \033[0m] disk caches flushed\n");
    if (mode == BS_SHUTDOWN_REBOOT) {
        console_print("[\033[96minfo\033[0m] restarting\n");
        reboot_machine();
    }
    console_print("[\033[96minfo\033[0m] powering off\n");
    acpi_poweroff();
    console_print("\nno software power-off on this machine. nothing is running: switch it off.\n");
    for (;;)
        __asm__ volatile("cli; hlt");
}
