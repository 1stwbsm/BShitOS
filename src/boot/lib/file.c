#include "lib/lib.h"

static const EFI_GUID loaded_image_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
static const EFI_GUID simple_fs_guid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
static const EFI_GUID file_info_guid = EFI_FILE_INFO_GUID;

EFI_STATUS file_open(const CHAR16 *path, EFI_FILE_PROTOCOL **file)
{
    EFI_LOADED_IMAGE_PROTOCOL *image;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs;
    EFI_FILE_PROTOCOL *root;
    EFI_STATUS status;

    status = gBS->HandleProtocol(gImage, &loaded_image_guid, (void **)&image);
    if (EFI_ERROR(status))
        return status;
    status = gBS->HandleProtocol(image->DeviceHandle, &simple_fs_guid, (void **)&fs);
    if (EFI_ERROR(status))
        return status;
    status = fs->OpenVolume(fs, &root);
    if (EFI_ERROR(status))
        return status;
    status = root->Open(root, file, (CHAR16 *)path, EFI_FILE_MODE_READ, 0);
    root->Close(root);
    return status;
}

EFI_STATUS file_size(EFI_FILE_PROTOCOL *file, UINT64 *size)
{
    /* EFI_FILE_INFO is followed by the variable-length file name */
    UINT64 info[64];
    UINTN length = sizeof(info);
    EFI_STATUS status = file->GetInfo(file, &file_info_guid, &length, info);

    if (!EFI_ERROR(status))
        *size = ((EFI_FILE_INFO *)info)->FileSize;
    return status;
}

EFI_STATUS file_load(const CHAR16 *path, void **data, UINTN *size)
{
    EFI_FILE_PROTOCOL *file;
    UINT64 file_bytes;
    UINTN length;
    EFI_STATUS status = file_open(path, &file);

    if (EFI_ERROR(status))
        return status;
    status = file_size(file, &file_bytes);
    if (!EFI_ERROR(status) && file_bytes > 0x7FFFFFFFu)
        status = EFI_OUT_OF_RESOURCES;
    if (EFI_ERROR(status))
        goto out;

    length = (UINTN)file_bytes;
    status = gBS->AllocatePool(EfiLoaderData, length ? length : 1, data);
    if (EFI_ERROR(status))
        goto out;
    status = file->Read(file, &length, *data);
    if (!EFI_ERROR(status) && length != file_bytes)
        status = EFI_LOAD_ERROR;
    if (EFI_ERROR(status))
        gBS->FreePool(*data);
    else
        *size = length;
out:
    file->Close(file);
    return status;
}
