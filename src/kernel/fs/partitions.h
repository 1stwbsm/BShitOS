#ifndef KERNEL_PARTITIONS_H
#define KERNEL_PARTITIONS_H

#include <stdint.h>

/* Finds this type's partition in the GPT. Returns 0 and its first LBA. */
int gpt_find_esp(uint64_t *lba);
int gpt_find_bsfs(uint64_t *lba);

/* Mount a partition as a VFS volume. */
int bsfs_mount_volume(const char *name, uint64_t lba);
int fat32_mount_volume(const char *name, uint64_t lba);

typedef int (*fat32_read_fn)(uint64_t lba, uint32_t sectors, void *buf);
int fat32_mount_device(const char *name, fat32_read_fn read, uint64_t lba);
int fat32_find(fat32_read_fn read, uint64_t *lba);
int iso9660_mount_volume(const char *name);
/* Mounts a BSFS image held in memory, for live systems. */
int bsfs_mount_ram_volume(const char *name, void *base, uint32_t size);
void bsfs_usage(uint32_t *total_kib, uint32_t *free_kib);

#endif
