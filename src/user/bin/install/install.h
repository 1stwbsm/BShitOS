/* install: puts the running live system on the SATA disk. */
#ifndef INSTALL_H
#define INSTALL_H

#include "bs.h"

#define SECTOR 512u
#define BLOCK 4096u
#define SECTORS_PER_BLOCK (BLOCK / SECTOR)
#define ESP_LBA 2048u
#define ALIGN_SECTORS 2048u /* 1 MiB */
#define ESP_SOURCE "/mnt/cd/ESP.IMG"

typedef unsigned long long u64;

/* What gets made, decided before anything is written. */
struct plan {
    u64 disk_sectors;
    u64 esp_sectors;
    u64 bsfs_lba, bsfs_sectors;
    char model[44];
};

/* What the new system's accounts look like. */
struct accounts {
    char name[BS_USER_NAME_MAX];
    char user_record[128], root_record[128]; /* from pass_make_record */
};

/* disk.c */
int disk_write_sectors(u64 lba, const void *data, unsigned int sectors);
int esp_size(u64 *sectors); /* from ESP.IMG's boot sector */
int write_partition_table(const struct plan *plan);
int copy_esp(const struct plan *plan);
void random_bytes(unsigned char *out, unsigned int count);
void progress(const char *what); /* rewrites the current status line */

/* bsfs.c */
int bsfs_install(const struct plan *plan, const struct accounts *accounts, unsigned int *files);

#endif
