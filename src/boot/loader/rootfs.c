#include <bshit/bsfs.h>

#include "config.h"
#include "lib/lib.h"
#include "loader/rootfs.h"

/* Firmware may need aligned transfer buffers. */
static UINT8 bounce[BSFS_BLOCK_SIZE] __attribute__((aligned(4096)));

static int read_block(void *ctx, uint32_t block, void *buf)
{
    EFI_BLOCK_IO_PROTOCOL *io = ctx;
    UINT64 lba = (UINT64)block * (BSFS_BLOCK_SIZE / io->Media->BlockSize);

    if (EFI_ERROR(io->ReadBlocks(io, io->Media->MediaId, lba, BSFS_BLOCK_SIZE, bounce)))
        return -1;
    memcpy(buf, bounce, BSFS_BLOCK_SIZE);
    return 0;
}

static void *ramdisk_base;
static UINTN ramdisk_size;

static int read_ramdisk(void *ctx, uint32_t block, void *buf)
{
    (void)ctx;
    if ((UINT64)block * BSFS_BLOCK_SIZE + BSFS_BLOCK_SIZE > ramdisk_size)
        return -1;
    memcpy(buf, (UINT8 *)ramdisk_base + block * BSFS_BLOCK_SIZE, BSFS_BLOCK_SIZE);
    return 0;
}

void rootfs_ramdisk(void **base, UINTN *size)
{
    *base = ramdisk_base;
    *size = ramdisk_size;
}

BOOLEAN rootfs_ramdisk_loaded(void)
{
    return ramdisk_base != 0;
}

static EFI_STATUS load_live_root(struct bsfs *fs)
{
    EFI_STATUS status = EFI_SUCCESS;

    if (!ramdisk_base)
        status = file_load(BSBOOT_LIVE_ROOT, &ramdisk_base, &ramdisk_size);
    if (EFI_ERROR(status)) {
        ramdisk_base = 0;
        return status;
    }
    fs->read_block = read_ramdisk;
    fs->write_block = 0;
    fs->ctx = 0;
    return bsfs_mount(fs) == 0 ? EFI_SUCCESS : EFI_LOAD_ERROR;
}

static EFI_STATUS find_partition(struct bsfs *fs)
{
    static const EFI_GUID block_io_guid = EFI_BLOCK_IO_PROTOCOL_GUID;
    EFI_HANDLE *handles;
    UINTN count;
    EFI_STATUS status = gBS->LocateHandleBuffer(ByProtocol, &block_io_guid, 0, &count, &handles);

    if (EFI_ERROR(status))
        return status;

    status = EFI_NOT_FOUND;
    for (UINTN i = 0; i < count && EFI_ERROR(status); i++) {
        EFI_BLOCK_IO_PROTOCOL *io;

        if (EFI_ERROR(gBS->HandleProtocol(handles[i], &block_io_guid, (void **)&io)))
            continue;
        if (!io->Media->LogicalPartition || !io->Media->MediaPresent ||
            io->Media->BlockSize == 0 || io->Media->BlockSize > BSFS_BLOCK_SIZE)
            continue;
        fs->read_block = read_block;
        fs->write_block = 0;
        fs->ctx = io;
        if (bsfs_mount(fs) == 0)
            status = EFI_SUCCESS;
    }
    gBS->FreePool(handles);
    return status;
}

static EFI_STATUS walk(struct bsfs *fs, const char *path, struct bsfs_inode *inode)
{
    char name[BSFS_NAME_MAX + 1];
    uint32_t ino = BSFS_ROOT_INO;

    for (;;) {
        UINTN length = 0;

        if (bsfs_read_inode(fs, ino, inode) < 0)
            return EFI_LOAD_ERROR;
        while (*path == '/')
            path++;
        if (!*path)
            return EFI_SUCCESS;
        while (*path && *path != '/') {
            if (length == BSFS_NAME_MAX)
                return EFI_NOT_FOUND;
            name[length++] = *path++;
        }
        name[length] = 0;
        if (inode->type != BSFS_TYPE_DIR || bsfs_lookup(fs, inode, name, &ino) != 1)
            return EFI_NOT_FOUND;
    }
}

EFI_STATUS rootfs_load(const char *path, void **data, UINTN *size)
{
    struct bsfs fs;
    struct bsfs_inode inode;
    UINTN length;
    EFI_STATUS status = find_partition(&fs);

    if (EFI_ERROR(status))
        status = load_live_root(&fs);
    if (EFI_ERROR(status))
        return status;
    status = walk(&fs, path, &inode);
    if (EFI_ERROR(status))
        return status;
    if (inode.type != BSFS_TYPE_FILE || inode.size > 0x7FFFFFFFu)
        return EFI_LOAD_ERROR;

    length = (UINTN)inode.size;
    status = gBS->AllocatePool(EfiLoaderData, length ? length : 1, data);
    if (EFI_ERROR(status))
        return status;
    if (bsfs_read(&fs, &inode, 0, *data, length) != (int)length) {
        gBS->FreePool(*data);
        return EFI_LOAD_ERROR;
    }
    *size = length;
    return EFI_SUCCESS;
}
