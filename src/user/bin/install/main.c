/*
 * install: puts BShitOS from the live ISO onto the SATA disk.
 *
 * The whole disk becomes: a GPT, an EFI System Partition with BSBOOT (copied
 * from ESP.IMG on the disc), and BSFS filling the rest, holding a copy of the
 * live system with fresh accounts: root and the one you name.
 */
#include <bshit/passhash.h>

#include "install.h"

#define MIN_BSFS_MIB 64u

static void fail(const char *what, int rc)
{
    printf("\n\033[91minstall: %s: %s\033[0m\n", what, strerror(rc));
}

static unsigned int mib(u64 sectors)
{
    return (unsigned int)(sectors >> 11);
}

static int valid_name(const char *name)
{
    unsigned int n = strlen(name);

    if (n == 0 || n >= BS_USER_NAME_MAX || name[0] == '-' || strcmp(name, "root") == 0)
        return 0;
    for (unsigned int i = 0; i < n; i++)
        if (!((name[i] >= 'a' && name[i] <= 'z') || (name[i] >= '0' && name[i] <= '9') || name[i] == '_' ||
              name[i] == '-'))
            return 0;
    return 1;
}

/* An echoed line from the keyboard. */
static int ask(const char *prompt, char *out, unsigned int size)
{
    unsigned int length = 0;

    printf("%s", prompt);
    for (;;) {
        int key = poll_key();

        if (key == '\n') {
            out[length] = 0;
            printf("\n");
            return (int)length;
        }
        if (key == 3) {
            printf("^C\n");
            return -1;
        }
        if (key == '\b' && length) {
            length--;
            printf("\b");
        } else if (key >= 0x20 && key < 0x7F && length + 1 < size) {
            out[length++] = (char)key;
            printf("%c", key);
        }
    }
}

/* Asks twice; makes the record. */
static int ask_password(const char *who, char *record)
{
    char first[128], again[128], prompt[64];
    unsigned char salt[PASS_SALT_BYTES];

    for (;;) {
        prompt[0] = 0;
        memcpy(prompt, who, strlen(who) + 1);
        memcpy(prompt + strlen(prompt), " password: ", 12);
        if (read_secret(prompt, first, sizeof(first)) < 0 || read_secret("again: ", again, sizeof(again)) < 0)
            return -1;
        if (strcmp(first, again) == 0)
            break;
        printf("\033[93mthey don't match, again\033[0m\n");
    }
    if (!first[0])
        printf("\033[93mempty: no password at all for %s\033[0m\n", who);
    random_bytes(salt, sizeof(salt));
    pass_make_record(first, salt, record, 128);
    memset(first, 0, sizeof(first));
    memset(again, 0, sizeof(again));
    return 0;
}

static int make_plan(struct plan *plan)
{
    struct bs_disk_info disk;
    u64 end;
    int rc;

    disk_info(&disk);
    if (!disk.present) {
        printf("\033[91minstall: no SATA disk found (attach one; IDE/NVMe/USB are not supported)\033[0m\n");
        return -1;
    }
    memcpy(plan->model, disk.model, sizeof(plan->model));
    plan->disk_sectors = (u64)disk.sectors_high << 32 | disk.sectors_low;
    rc = esp_size(&plan->esp_sectors);
    if (rc < 0) {
        fail(ESP_SOURCE " (is the live CD in?)", rc);
        return -1;
    }
    plan->bsfs_lba = (ESP_LBA + plan->esp_sectors + ALIGN_SECTORS - 1) & ~(u64)(ALIGN_SECTORS - 1);
    end = plan->disk_sectors - 34; /* backup GPT: 32 entry sectors and a header, plus one spare */
    if (end <= plan->bsfs_lba || mib(end - plan->bsfs_lba) < MIN_BSFS_MIB) {
        printf("\033[91minstall: the disk is %u MiB; it needs at least %u\033[0m\n", mib(plan->disk_sectors),
               mib(plan->bsfs_lba) + MIN_BSFS_MIB + 1);
        return -1;
    }
    plan->bsfs_sectors = (end - plan->bsfs_lba) & ~(u64)(SECTORS_PER_BLOCK - 1);
    return 0;
}

/* "[n/3] what " then a status spot that progress() rewrites until done(). */
static void begin(int n, const char *what)
{
    printf("[%d/3] %s \033[s", n, what);
}

static int done(const char *what, int rc)
{
    if (rc < 0) {
        fail(what, rc);
        return rc;
    }
    printf("\033[u\033[K\033[92mok\033[0m\n");
    return 0;
}

int main(void)
{
    struct bs_user me;
    struct bs_sysinfo info;
    struct plan plan;
    struct accounts accounts;
    char answer[16];
    unsigned int files = 0;

    whoami(&me);
    sysinfo(&info);
    if (!(info.flags & BS_SYSINFO_LIVE)) {
        printf("\033[91minstall: this system runs from the disk; boot the live ISO to install\033[0m\n");
        return 1;
    }
    if (me.uid != BS_UID_ROOT) {
        printf("\033[91minstall: root only. on the live ISO: su -c install\033[0m\n");
        return 1;
    }
    if (make_plan(&plan) < 0)
        return 1;

    printf("\033[1;93mBShitOS installer\033[0m\n");
    printf("  disk    %s, %u MiB\n", plan.model[0] ? plan.model : "SATA disk", mib(plan.disk_sectors));
    printf("  layout  GPT: ESP %u MiB (BSBOOT) + BSFS %u MiB (the rest)\n", mib(plan.esp_sectors),
           mib(plan.bsfs_sectors));
    printf("  copies  this live system, with new accounts; /userspace starts empty\n\n");

    do {
        if (ask("your account name: ", accounts.name, sizeof(accounts.name)) < 0)
            return 1;
        if (!valid_name(accounts.name))
            printf("\033[93ma-z 0-9 _ -, under 32 characters, not root\033[0m\n");
    } while (!valid_name(accounts.name));
    if (ask_password(accounts.name, accounts.user_record) < 0 || ask_password("root", accounts.root_record) < 0)
        return 1;

    printf("\nthis \033[91merases everything\033[0m on %s. type 'erase' to go on: ",
           plan.model[0] ? plan.model : "the disk");
    if (ask("", answer, sizeof(answer)) < 0 || strcmp(answer, "erase") != 0) {
        printf("nothing written\n");
        return 1;
    }
    printf("\n");

    begin(1, "partition table");
    if (done("partition table", write_partition_table(&plan)) < 0)
        return 1;
    begin(2, "boot partition ");
    if (done("boot partition", copy_esp(&plan)) < 0)
        return 1;
    begin(3, "filesystem     ");
    if (done("filesystem", bsfs_install(&plan, &accounts, &files)) < 0)
        return 1;
    printf("\n%u files installed. log in as %s.\n", files, accounts.name);
    printf("power off, take the CD out, and boot from the disk. reboot now? [y/N] ");
    if (ask("", answer, sizeof(answer)) > 0 && (answer[0] == 'y' || answer[0] == 'Y'))
        reboot();
    return 0;
}
