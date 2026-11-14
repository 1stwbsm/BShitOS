#include "actions/actions.h"
#include "config.h"
#include "lib/lib.h"

/* The file GUID edk2 firmware stores its built-in UEFI Shell under. */
static const EFI_GUID builtin_shell_file =
    {0x7C04A583, 0x9E3E, 0x4F1C, {0xAD, 0x65, 0xE0, 0x52, 0x68, 0xD0, 0xB4, 0xD1}};
static const EFI_GUID fv2_guid = EFI_FIRMWARE_VOLUME2_PROTOCOL_GUID;
static const EFI_GUID device_path_guid = EFI_DEVICE_PATH_PROTOCOL_GUID;

#define DP_END_TYPE 0x7F
#define DP_END_ENTIRE 0xFF
#define DP_MEDIA_TYPE 0x04
#define DP_MEDIA_FV_FILE 0x06

static UINTN device_path_size(const EFI_DEVICE_PATH_PROTOCOL *dp)
{
    const UINT8 *start = (const UINT8 *)dp, *p = start;

    while (!(p[0] == DP_END_TYPE && p[1] == DP_END_ENTIRE))
        p += p[2] | (UINTN)p[3] << 8;
    return (UINTN)(p - start);
}

/* Builds <volume path>/FvFile(guid)/End and tries to load it. */
static EFI_STATUS load_from_volume(EFI_HANDLE volume, EFI_HANDLE *loaded)
{
    EFI_DEVICE_PATH_PROTOCOL *volume_path;
    UINT8 *path;
    UINTN prefix;
    EFI_STATUS status;

    status = gBS->HandleProtocol(volume, &device_path_guid, (void **)&volume_path);
    if (EFI_ERROR(status))
        return status;

    prefix = device_path_size(volume_path);
    status = gBS->AllocatePool(EfiLoaderData, prefix + 4 + sizeof(EFI_GUID) + 4, (void **)&path);
    if (EFI_ERROR(status))
        return status;

    UINT8 *p = path;
    memcpy(p, volume_path, prefix);
    p += prefix;
    *p++ = DP_MEDIA_TYPE;
    *p++ = DP_MEDIA_FV_FILE;
    *p++ = 4 + sizeof(EFI_GUID);
    *p++ = 0;
    memcpy(p, &builtin_shell_file, sizeof(EFI_GUID));
    p += sizeof(EFI_GUID);
    *p++ = DP_END_TYPE;
    *p++ = DP_END_ENTIRE;
    *p++ = 4;
    *p++ = 0;

    status = gBS->LoadImage(0, gImage, (EFI_DEVICE_PATH_PROTOCOL *)path, 0, 0, loaded);
    gBS->FreePool(path);
    return status;
}

static EFI_STATUS load_builtin_shell(EFI_HANDLE *loaded)
{
    EFI_HANDLE *volumes;
    UINTN count;
    EFI_STATUS status;

    status = gBS->LocateHandleBuffer(ByProtocol, &fv2_guid, 0, &count, &volumes);
    if (EFI_ERROR(status))
        return status;

    status = EFI_NOT_FOUND;
    for (UINTN i = 0; i < count && EFI_ERROR(status); i++)
        status = load_from_volume(volumes[i], loaded);
    gBS->FreePool(volumes);
    return status;
}

static EFI_STATUS load_disk_shell(EFI_HANDLE *loaded)
{
    void *data;
    UINTN size;
    EFI_STATUS status = file_load(BSBOOT_SHELL_PATH, &data, &size);

    if (EFI_ERROR(status))
        return status;
    status = gBS->LoadImage(0, gImage, 0, data, size, loaded);
    gBS->FreePool(data);
    return status;
}

void action_efi_shell(void)
{
    EFI_HANDLE shell;
    EFI_STATUS status = load_disk_shell(&shell);

    if (EFI_ERROR(status))
        status = load_builtin_shell(&shell);
    if (EFI_ERROR(status)) {
        con_error(L"No EFI shell available", L"Put one at " BSBOOT_SHELL_PATH, status);
        return;
    }

    con_clear();
    gST->ConOut->EnableCursor(gST->ConOut, 1);
    status = gBS->StartImage(shell, 0, 0);
    if (EFI_ERROR(status))
        con_error(L"The EFI shell exited with an error", 0, status);
}
