#include <bshit/bootinfo.h>

#include "arch/x86/cpu.h"
#include "conf.h"
#include "drivers/acpi.h"
#include "drivers/ahci.h"
#include "drivers/block.h"
#include "proc/shutdown.h"
#include "proc/memory.h"
#include "arch/x86/cpuinfo.h"
#include "drivers/console.h"
#include "drivers/rtc.h"
#include "drivers/xhci.h"
#include "fs/partitions.h"
#include "lib/string.h"
#include "net/net.h"
#include "proc/proc.h"

#define DEFAULT_SHELL "/bin/login.bs"
#define CONFIG_VALUE_MAX 64

static char shell_path[BS_PATH_MAX] = DEFAULT_SHELL;
extern uint32_t boot_seconds;
extern int system_live;

static void halt(const char *reason)
{
    console_set_serial(1);
    console_print("\nkernel halted: ");
    console_print(reason);
    console_print("\n");
    for (;;)
        __asm__ volatile("cli; hlt");
}

static void log_ok(const char *text)
{
    console_print("[\033[92m ok \033[0m] ");
    console_print(text);
    console_print("\n");
}

static void log_warn(const char *text)
{
    console_print("[\033[93mwarn\033[0m] ");
    console_print(text);
    console_print("\n");
}

static uint32_t usable_mib(const struct bootinfo *info)
{
    const struct bootinfo_mem_range *ranges =
        (const struct bootinfo_mem_range *)(uintptr_t)info->mem_ranges;
    uint64_t total = 0;

    for (uint32_t i = 0; i < info->mem_range_count; i++)
        if (ranges[i].type == BOOTINFO_MEM_USABLE)
            total += ranges[i].length;
    return (uint32_t)(total >> 20);
}

static void log_hardware(const struct bootinfo *info)
{
    console_print("[\033[96minfo\033[0m] boot protocol v");
    console_print_u32(info->version);
    console_print(", ");
    console_print_u32(info->mem_range_count);
    console_print(" memory ranges, ");
    console_print_u32(usable_mib(info));
    console_print(" MiB usable\n");

    console_print(info->framebuffer.format == BOOTINFO_FB_TEXT ? "[\033[96minfo\033[0m] VGA text mode "
                                                              : "[\033[96minfo\033[0m] framebuffer ");
    console_print_u32(info->framebuffer.width);
    console_print("x");
    console_print_u32(info->framebuffer.height);
    console_print(" at 0x");
    console_print_hex((uint32_t)info->framebuffer.base);
    console_print("\n");

    console_print("[\033[96minfo\033[0m] ACPI tables at 0x");
    console_print_hex((uint32_t)info->acpi_rsdp);
    console_print("\n");
}

static void log_cpu_and_memory(void)
{
    uint32_t total, free;
    static const char *const families[] = {"386", "486", "Pentium", "P6-class"};

    console_print("[\033[96minfo\033[0m] CPU: ");
    if (cpu.family >= 3)
        console_print(families[cpu.family > 6 ? 3 : cpu.family - 3]);
    if (cpu.vendor[0]) {
        console_print(" (");
        console_print(cpu.vendor);
        console_print(")");
    }
    console_print(cpu.has_pse ? ", 4 MiB pages" : ", 4 KiB pages only");
    console_print(cpu.has_tsc ? "\n" : ", no cycle counter\n");
    mem_stats(&total, &free);
    console_print("[\033[92m ok \033[0m] memory: ");
    console_print_u32(free / 1024);
    console_print(" MiB free for programs, paged in as they touch it\n");
}

static void print_ip(uint32_t ip)
{
    console_print_u32(ip >> 24);
    console_print(".");
    console_print_u32((ip >> 16) & 255);
    console_print(".");
    console_print_u32((ip >> 8) & 255);
    console_print(".");
    console_print_u32(ip & 255);
}

static void mount_volumes(const struct bootinfo *info)
{
    uint64_t lba;
    int live = info->version >= 2 && info->ramdisk_base != 0;

    system_live = live;

    if (ahci_init() < 0)
        console_print("[\033[96minfo\033[0m] no AHCI controller; looking for IDE\n");
    else
        log_ok("AHCI storage controller started");
    if (block_init() == 0) {
        console_print("[\033[92m ok \033[0m] ");
        console_print(block_kind());
        console_print(" disk found\n");
    }
    if (ahci_has_cd())
        log_ok("CD/DVD drive found");

    /* The root must be the first volume mounted. */
    if (live) {
        if (bsfs_mount_ram_volume("root", (void *)(uintptr_t)info->ramdisk_base,
                                  (uint32_t)info->ramdisk_size) < 0)
            halt("could not mount the live root filesystem");
        log_ok("root filesystem mounted from the live image in memory");
        log_warn("running live: changes will not be saved");
    } else {
        if (!block_present())
            halt("no disk found");
        if (gpt_find_bsfs(&lba) < 0)
            halt("no BSFS partition found");
        if (bsfs_mount_volume("root", lba) < 0)
            halt("could not mount the root filesystem");
        log_ok("root filesystem mounted (BSFS)");
        if (gpt_find_esp(&lba) < 0 || fat32_mount_volume("esp", lba) < 0)
            log_warn("boot partition unavailable, /boot/efi will not resolve");
        else
            log_ok("boot partition mounted at /boot/efi (FAT32)");
    }

    if (ahci_has_cd() && iso9660_mount_volume("cd") == 0)
        log_ok("disc mounted at /mnt/cd (ISO9660)");
}

static void apply_config(void)
{
    char value[CONFIG_VALUE_MAX];
    uint32_t fg = 0xD8D8D8, bg = 0x000000;

    kconf_load("/conf/kernel.conf");
    log_ok("settings read from /conf/kernel.conf");
    if (kconf_get("console_fg", value, sizeof(value)) == 0)
        fg = kconf_hex(value);
    if (kconf_get("console_bg", value, sizeof(value)) == 0)
        bg = kconf_hex(value);
    console_set_colors(fg, bg);
    if (kconf_get("serial", value, sizeof(value)) == 0 && strcmp(value, "on") == 0)
        console_set_serial(1);
    kconf_get("init", shell_path, sizeof(shell_path));
}

static void start_network(void)
{
    char value[CONFIG_VALUE_MAX];

    if (kconf_get("network", value, sizeof(value)) == 0 && strcmp(value, "off") == 0)
        return;
    net_init();
    {
        struct bs_netinfo info;

        net_info(&info);
        if (!info.present) {
            log_warn("no supported network card found");
            return;
        }
        console_print("[\033[96minfo\033[0m] requesting an address over DHCP\n");
        if (net_dhcp() < 0) {
            log_warn("DHCP did not answer, the network is unavailable");
            return;
        }
        net_info(&info);
        console_print("[\033[92m ok \033[0m] network up: address ");
        print_ip(info.ip);
        console_print(", gateway ");
        print_ip(info.gateway);
        console_print(", DNS ");
        print_ip(info.dns);
        console_print("\n");
    }
}

static void start_usb(void)
{
    uint64_t lba;

    usb_init();
    if (usb_storage_present() && fat32_find(usb_storage_read, &lba) == 0 &&
        fat32_mount_device("usb", usb_storage_read, lba) == 0)
        log_ok("USB drive mounted at /mnt/usb (FAT32)");
}

void kmain(const struct bootinfo *info)
{
    char *shell_argv[] = {shell_path};

    if (info->magic != BOOTINFO_MAGIC || info->version < 1)
        for (;;)
            __asm__ volatile("cli; hlt");

    gdt_init();
    boot_seconds = rtc_seconds();
    idt_init();
    cpu_detect();
    mem_init(info);
    paging_init();
    console_init(&info->framebuffer);
    console_print("BShitOS kernel starting\n");
    log_ok("CPU tables, interrupts and paging set up");
    log_ok("console started");
    log_hardware(info);

    log_cpu_and_memory();
    mount_volumes(info);
    apply_config();
    acpi_init(info->acpi_rsdp);
    if (acpi_power_available())
        log_ok("ACPI ready: power button and soft-off enabled");
    else
        log_warn("ACPI soft-off is unavailable");
    start_usb();
    start_network();

    console_print("[\033[96minfo\033[0m] starting ");
    console_print(shell_path);
    console_print("\n\n");

    for (;;) {
        int code = proc_run(shell_path, 1, shell_argv);

        if (shutdown_pending())
            shutdown_finish();
        if (code < 0)
            halt("could not start the init= program named in /conf/kernel.conf");
        console_print("\n");
    }
}
