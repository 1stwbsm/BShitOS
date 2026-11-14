#include <bshit/bootinfo.h>

#include "actions/actions.h"
#include "config.h"
#include "lib/lib.h"
#include "loader/elf.h"
#include "loader/rootfs.h"

#define MAP_SLACK_DESCRIPTORS 32
#define BOOT_MESSAGE_PAUSE_US 400000

static const EFI_GUID gop_guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
static const EFI_GUID acpi20_guid = EFI_ACPI_20_TABLE_GUID;
static const EFI_GUID acpi10_guid = EFI_ACPI_10_TABLE_GUID;

static void fill_framebuffer(struct bootinfo_framebuffer *fb)
{
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop;

    fb->format = BOOTINFO_FB_NONE;
    if (EFI_ERROR(gBS->LocateProtocol(&gop_guid, 0, (void **)&gop)))
        return;

    switch (gop->Mode->Info->PixelFormat) {
    case PixelRedGreenBlueReserved8BitPerColor:
        fb->format = BOOTINFO_FB_RGBX;
        break;
    case PixelBlueGreenRedReserved8BitPerColor:
        fb->format = BOOTINFO_FB_BGRX;
        break;
    default:
        return;
    }
    fb->base = gop->Mode->FrameBufferBase;
    fb->width = gop->Mode->Info->HorizontalResolution;
    fb->height = gop->Mode->Info->VerticalResolution;
    fb->pixels_per_line = gop->Mode->Info->PixelsPerScanLine;
}

static UINT64 find_rsdp(void)
{
    UINT64 v1 = 0;

    for (UINTN i = 0; i < gST->NumberOfTableEntries; i++) {
        const EFI_CONFIGURATION_TABLE *t = &gST->ConfigurationTable[i];

        if (memcmp(&t->VendorGuid, &acpi20_guid, sizeof(EFI_GUID)) == 0)
            return (UINTN)t->VendorTable;
        if (memcmp(&t->VendorGuid, &acpi10_guid, sizeof(EFI_GUID)) == 0)
            v1 = (UINTN)t->VendorTable;
    }
    return v1;
}

static UINT32 classify(UINT32 efi_type)
{
    switch (efi_type) {
    case EfiConventionalMemory:
    case EfiBootServicesCode:
    case EfiBootServicesData:
        return BOOTINFO_MEM_USABLE;
    case EfiLoaderCode:
    case EfiLoaderData:
        return BOOTINFO_MEM_BOOTLOADER;
    case EfiACPIReclaimMemory:
        return BOOTINFO_MEM_ACPI;
    default:
        return BOOTINFO_MEM_RESERVED;
    }
}

/* Runs after ExitBootServices: must not call the firmware or allocate. */
static UINT32 convert_memory_map(const UINT8 *map, UINTN map_size, UINTN desc_size,
                                 struct bootinfo_mem_range *ranges)
{
    UINT32 count = 0;

    for (UINTN off = 0; off + desc_size <= map_size; off += desc_size) {
        const EFI_MEMORY_DESCRIPTOR *d = (const EFI_MEMORY_DESCRIPTOR *)(map + off);
        UINT64 length = d->NumberOfPages * 4096;
        UINT32 type = classify(d->Type);

        if (count && ranges[count - 1].type == type &&
            ranges[count - 1].base + ranges[count - 1].length == d->PhysicalStart) {
            ranges[count - 1].length += length;
            continue;
        }
        ranges[count].base = d->PhysicalStart;
        ranges[count].length = length;
        ranges[count].type = type;
        ranges[count].reserved = 0;
        count++;
    }
    return count;
}

/* Only returns on failure. */
static EFI_STATUS handoff(UINT32 entry)
{
    struct bootinfo *info;
    struct bootinfo_mem_range *ranges;
    EFI_MEMORY_DESCRIPTOR *map;
    UINTN map_size = 0, capacity, key, desc_size;
    UINT32 desc_version;
    EFI_STATUS status;

    status = gBS->GetMemoryMap(&map_size, 0, &key, &desc_size, &desc_version);
    if (status != EFI_BUFFER_TOO_SMALL)
        return EFI_ERROR(status) ? status : EFI_DEVICE_ERROR;

    /* Our own allocations below split descriptors, so leave room to grow. */
    capacity = map_size + MAP_SLACK_DESCRIPTORS * desc_size;
    status = gBS->AllocatePool(EfiLoaderData, capacity, (void **)&map);
    if (EFI_ERROR(status))
        return status;
    status = gBS->AllocatePool(EfiLoaderData,
                               sizeof(*info) + capacity / desc_size * sizeof(*ranges),
                               (void **)&info);
    if (EFI_ERROR(status))
        return status;

    ranges = (struct bootinfo_mem_range *)(info + 1);
    memset(info, 0, sizeof(*info));
    info->magic = BOOTINFO_MAGIC;
    info->version = BOOTINFO_VERSION;
    info->size = sizeof(*info);
    info->mem_ranges = (UINTN)ranges;
    info->acpi_rsdp = find_rsdp();
    {
        void *ramdisk;
        UINTN ramdisk_size;

        rootfs_ramdisk(&ramdisk, &ramdisk_size);
        info->ramdisk_base = (UINTN)ramdisk;
        info->ramdisk_size = ramdisk_size;
    }
    fill_framebuffer(&info->framebuffer);

    /* ExitBootServices fails if the map changed since we read it: retry. */
    for (int attempt = 0;; attempt++) {
        map_size = capacity;
        status = gBS->GetMemoryMap(&map_size, map, &key, &desc_size, &desc_version);
        if (EFI_ERROR(status))
            return status;
        status = gBS->ExitBootServices(gImage, key);
        if (!EFI_ERROR(status))
            break;
        if (attempt == 4)
            return status;
    }

    info->mem_range_count = convert_memory_map((const UINT8 *)map, map_size, desc_size, ranges);

    __asm__ volatile("cli");
    ((void (*)(struct bootinfo *))(UINTN)entry)(info);
    for (;;)
        __asm__ volatile("hlt");
}

void action_boot_kernel(void)
{
    void *image;
    UINTN size;
    UINT32 entry;
    EFI_STATUS status;

    con_clear();
    con_print(L"BSBOOT: reading " BSBOOT_KERNEL_LABEL L"\r\n");
    status = rootfs_load(BSBOOT_KERNEL_PATH, &image, &size);
    if (EFI_ERROR(status)) {
        con_error(L"Could not read the kernel from BSFS", BSBOOT_KERNEL_LABEL, status);
        return;
    }

    con_print(L"BSBOOT: kernel is ");
    con_print_u32((UINT32)size, 1);
    con_print(L" bytes\r\n");
    if (rootfs_ramdisk_loaded())
        con_print(L"BSBOOT: live mode, root filesystem loaded into memory\r\n");

    status = elf_load(image, size, &entry);
    gBS->FreePool(image);
    if (EFI_ERROR(status)) {
        con_error(L"Could not load the kernel image", BSBOOT_KERNEL_LABEL, status);
        return;
    }

    con_print(L"BSBOOT: kernel loaded, entry point at physical address ");
    con_print_u32(entry, 1);
    con_print(L"\r\nBSBOOT: leaving firmware and starting the kernel\r\n");
    gBS->Stall(BOOT_MESSAGE_PAUSE_US);

    status = handoff(entry);
    con_error(L"Could not hand off to the kernel", 0, status);
}
