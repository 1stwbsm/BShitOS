/*
 * The subset of the UEFI specification BSBOOT uses, for 32-bit (IA32) firmware.
 * Field order inside the protocol tables comes from the spec: functions we do
 * not call stay as `void *` placeholders to preserve offsets.
 */
#ifndef BSBOOT_EFI_H
#define BSBOOT_EFI_H

#include <stdint.h>

typedef uint8_t  UINT8;
typedef uint16_t UINT16;
typedef uint32_t UINT32;
typedef uint64_t UINT64;
typedef int32_t  INT32;
typedef uint32_t UINTN;
typedef uint8_t  BOOLEAN;
typedef unsigned short CHAR16;
typedef UINTN    EFI_STATUS;
typedef void    *EFI_HANDLE;
typedef void    *EFI_EVENT;
typedef UINT64   EFI_PHYSICAL_ADDRESS;

#define EFIAPI /* IA32 UEFI uses plain cdecl */

#define EFI_ERROR_BIT         0x80000000u
#define EFI_ERROR(s)          (((s) & EFI_ERROR_BIT) != 0)
#define EFI_SUCCESS           0u
#define EFI_INVALID_PARAMETER (EFI_ERROR_BIT | 2)
#define EFI_UNSUPPORTED       (EFI_ERROR_BIT | 3)
#define EFI_BUFFER_TOO_SMALL  (EFI_ERROR_BIT | 5)
#define EFI_NOT_READY         (EFI_ERROR_BIT | 6)
#define EFI_DEVICE_ERROR      (EFI_ERROR_BIT | 7)
#define EFI_LOAD_ERROR        (EFI_ERROR_BIT | 1)
#define EFI_OUT_OF_RESOURCES  (EFI_ERROR_BIT | 9)
#define EFI_NOT_FOUND         (EFI_ERROR_BIT | 14)
#define EFI_TIMEOUT           (EFI_ERROR_BIT | 18)

typedef struct {
    UINT32 Data1;
    UINT16 Data2;
    UINT16 Data3;
    UINT8  Data4[8];
} EFI_GUID;

#define EFI_LOADED_IMAGE_PROTOCOL_GUID \
    {0x5B1B31A1, 0x9562, 0x11D2, {0x8E, 0x3F, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B}}
#define EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID \
    {0x964E5B22, 0x6459, 0x11D2, {0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B}}
#define EFI_FILE_INFO_GUID \
    {0x09576E92, 0x6D3F, 0x11D2, {0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B}}
#define EFI_DEVICE_PATH_PROTOCOL_GUID \
    {0x09576E91, 0x6D3F, 0x11D2, {0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B}}
#define EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID \
    {0x9042A9DE, 0x23DC, 0x4A38, {0x96, 0xFB, 0x7A, 0xDE, 0xD0, 0x80, 0x51, 0x6A}}
#define EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL_GUID \
    {0x387477C2, 0x69C7, 0x11D2, {0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B}}
#define EFI_PCI_IO_PROTOCOL_GUID \
    {0x4CF5B200, 0x68B8, 0x4CA5, {0x9E, 0xEC, 0xB2, 0x3E, 0x3F, 0x50, 0x02, 0x9A}}
#define EFI_FIRMWARE_VOLUME2_PROTOCOL_GUID \
    {0x220E73B6, 0x6BDB, 0x4413, {0x84, 0x05, 0xB9, 0x74, 0xB1, 0x08, 0x61, 0x9A}}
#define EFI_ACPI_20_TABLE_GUID \
    {0x8868E871, 0xE4F1, 0x11D3, {0xBC, 0x22, 0x00, 0x80, 0xC7, 0x3C, 0x88, 0x81}}
#define EFI_ACPI_10_TABLE_GUID \
    {0xEB9D2D30, 0x2D88, 0x11D3, {0x9A, 0x16, 0x00, 0x90, 0x27, 0x3F, 0xC1, 0x4D}}

typedef struct {
    UINT64 Signature;
    UINT32 Revision;
    UINT32 HeaderSize;
    UINT32 CRC32;
    UINT32 Reserved;
} EFI_TABLE_HEADER;

/* ---- Console ---------------------------------------------------------- */

typedef struct {
    UINT16 ScanCode;
    CHAR16 UnicodeChar;
} EFI_INPUT_KEY;

#define SCAN_UP   0x01
#define SCAN_DOWN 0x02
#define SCAN_ESC  0x17

typedef struct EFI_SIMPLE_TEXT_INPUT_PROTOCOL {
    void *Reset;
    EFI_STATUS (EFIAPI *ReadKeyStroke)(struct EFI_SIMPLE_TEXT_INPUT_PROTOCOL *, EFI_INPUT_KEY *);
    EFI_EVENT WaitForKey;
} EFI_SIMPLE_TEXT_INPUT_PROTOCOL;

typedef struct {
    INT32 MaxMode;
    INT32 Mode;
    INT32 Attribute;
    INT32 CursorColumn;
    INT32 CursorRow;
    BOOLEAN CursorVisible;
} SIMPLE_TEXT_OUTPUT_MODE;

typedef struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
    void *Reset;
    EFI_STATUS (EFIAPI *OutputString)(struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *, CHAR16 *);
    void *TestString;
    EFI_STATUS (EFIAPI *QueryMode)(struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *, UINTN, UINTN *, UINTN *);
    void *SetMode;
    EFI_STATUS (EFIAPI *SetAttribute)(struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *, UINTN);
    EFI_STATUS (EFIAPI *ClearScreen)(struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *);
    EFI_STATUS (EFIAPI *SetCursorPosition)(struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *, UINTN, UINTN);
    EFI_STATUS (EFIAPI *EnableCursor)(struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *, BOOLEAN);
    SIMPLE_TEXT_OUTPUT_MODE *Mode;
} EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;

#define EFI_BLACK        0x0
#define EFI_BLUE         0x1
#define EFI_GREEN        0x2
#define EFI_CYAN         0x3
#define EFI_RED          0x4
#define EFI_MAGENTA      0x5
#define EFI_LIGHTGRAY    0x7
#define EFI_DARKGRAY     0x8
#define EFI_LIGHTCYAN    0xB
#define EFI_LIGHTRED     0xC
#define EFI_LIGHTMAGENTA 0xD
#define EFI_YELLOW       0xE
#define EFI_WHITE        0xF
#define EFI_ATTR(fg, bg) ((UINTN)(fg) | ((UINTN)(bg) << 4))

/* ---- Boot services ---------------------------------------------------- */

typedef enum { AllocateAnyPages, AllocateMaxAddress, AllocateAddress } EFI_ALLOCATE_TYPE;

typedef enum {
    EfiReservedMemoryType,
    EfiLoaderCode,
    EfiLoaderData,
    EfiBootServicesCode,
    EfiBootServicesData,
    EfiRuntimeServicesCode,
    EfiRuntimeServicesData,
    EfiConventionalMemory,
    EfiUnusableMemory,
    EfiACPIReclaimMemory,
    EfiACPIMemoryNVS,
    EfiMemoryMappedIO,
    EfiMemoryMappedIOPortSpace,
    EfiPalCode,
    EfiPersistentMemory,
} EFI_MEMORY_TYPE;

typedef struct {
    UINT32 Type;
    UINT32 Pad;
    UINT64 PhysicalStart;
    UINT64 VirtualStart;
    UINT64 NumberOfPages;
    UINT64 Attribute;
} EFI_MEMORY_DESCRIPTOR;

typedef enum { AllHandles, ByRegisterNotify, ByProtocol } EFI_LOCATE_SEARCH_TYPE;

typedef struct {
    UINT8 Type;
    UINT8 SubType;
    UINT8 Length[2];
} EFI_DEVICE_PATH_PROTOCOL;

typedef struct {
    EFI_TABLE_HEADER Hdr;
    void *RaiseTPL;
    void *RestoreTPL;
    EFI_STATUS (EFIAPI *AllocatePages)(EFI_ALLOCATE_TYPE, EFI_MEMORY_TYPE, UINTN, EFI_PHYSICAL_ADDRESS *);
    EFI_STATUS (EFIAPI *FreePages)(EFI_PHYSICAL_ADDRESS, UINTN);
    EFI_STATUS (EFIAPI *GetMemoryMap)(UINTN *, EFI_MEMORY_DESCRIPTOR *, UINTN *, UINTN *, UINT32 *);
    EFI_STATUS (EFIAPI *AllocatePool)(EFI_MEMORY_TYPE, UINTN, void **);
    EFI_STATUS (EFIAPI *FreePool)(void *);
    void *CreateEvent;
    void *SetTimer;
    EFI_STATUS (EFIAPI *WaitForEvent)(UINTN, EFI_EVENT *, UINTN *);
    void *SignalEvent;
    void *CloseEvent;
    void *CheckEvent;
    void *InstallProtocolInterface;
    void *ReinstallProtocolInterface;
    void *UninstallProtocolInterface;
    EFI_STATUS (EFIAPI *HandleProtocol)(EFI_HANDLE, const EFI_GUID *, void **);
    void *Reserved;
    void *RegisterProtocolNotify;
    void *LocateHandle;
    void *LocateDevicePath;
    void *InstallConfigurationTable;
    EFI_STATUS (EFIAPI *LoadImage)(BOOLEAN, EFI_HANDLE, EFI_DEVICE_PATH_PROTOCOL *, void *, UINTN, EFI_HANDLE *);
    EFI_STATUS (EFIAPI *StartImage)(EFI_HANDLE, UINTN *, CHAR16 **);
    void *Exit;
    EFI_STATUS (EFIAPI *UnloadImage)(EFI_HANDLE);
    EFI_STATUS (EFIAPI *ExitBootServices)(EFI_HANDLE, UINTN);
    void *GetNextMonotonicCount;
    EFI_STATUS (EFIAPI *Stall)(UINTN);
    EFI_STATUS (EFIAPI *SetWatchdogTimer)(UINTN, UINT64, UINTN, CHAR16 *);
    void *ConnectController;
    EFI_STATUS (EFIAPI *DisconnectController)(EFI_HANDLE, EFI_HANDLE, EFI_HANDLE);
    void *OpenProtocol;
    void *CloseProtocol;
    void *OpenProtocolInformation;
    void *ProtocolsPerHandle;
    EFI_STATUS (EFIAPI *LocateHandleBuffer)(EFI_LOCATE_SEARCH_TYPE, const EFI_GUID *, void *, UINTN *, EFI_HANDLE **);
    EFI_STATUS (EFIAPI *LocateProtocol)(const EFI_GUID *, void *, void **);
} EFI_BOOT_SERVICES;

typedef enum { EfiResetCold, EfiResetWarm, EfiResetShutdown } EFI_RESET_TYPE;

typedef struct {
    EFI_TABLE_HEADER Hdr;
    void *GetTime;
    void *SetTime;
    void *GetWakeupTime;
    void *SetWakeupTime;
    void *SetVirtualAddressMap;
    void *ConvertPointer;
    void *GetVariable;
    void *GetNextVariableName;
    void *SetVariable;
    void *GetNextHighMonotonicCount;
    void (EFIAPI *ResetSystem)(EFI_RESET_TYPE, EFI_STATUS, UINTN, void *);
} EFI_RUNTIME_SERVICES;

typedef struct {
    EFI_GUID VendorGuid;
    void *VendorTable;
} EFI_CONFIGURATION_TABLE;

typedef struct {
    EFI_TABLE_HEADER Hdr;
    CHAR16 *FirmwareVendor;
    UINT32 FirmwareRevision;
    EFI_HANDLE ConsoleInHandle;
    EFI_SIMPLE_TEXT_INPUT_PROTOCOL *ConIn;
    EFI_HANDLE ConsoleOutHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *ConOut;
    EFI_HANDLE StandardErrorHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *StdErr;
    EFI_RUNTIME_SERVICES *RuntimeServices;
    EFI_BOOT_SERVICES *BootServices;
    UINTN NumberOfTableEntries;
    EFI_CONFIGURATION_TABLE *ConfigurationTable;
} EFI_SYSTEM_TABLE;

/* ---- Files ------------------------------------------------------------ */

typedef struct {
    UINT32 Revision;
    EFI_HANDLE ParentHandle;
    EFI_SYSTEM_TABLE *SystemTable;
    EFI_HANDLE DeviceHandle;
    /* remaining fields unused */
} EFI_LOADED_IMAGE_PROTOCOL;

#define EFI_FILE_MODE_READ 0x1ULL

typedef struct EFI_FILE_PROTOCOL {
    UINT64 Revision;
    EFI_STATUS (EFIAPI *Open)(struct EFI_FILE_PROTOCOL *, struct EFI_FILE_PROTOCOL **, CHAR16 *, UINT64, UINT64);
    EFI_STATUS (EFIAPI *Close)(struct EFI_FILE_PROTOCOL *);
    void *Delete;
    EFI_STATUS (EFIAPI *Read)(struct EFI_FILE_PROTOCOL *, UINTN *, void *);
    void *Write;
    void *GetPosition;
    void *SetPosition;
    EFI_STATUS (EFIAPI *GetInfo)(struct EFI_FILE_PROTOCOL *, const EFI_GUID *, UINTN *, void *);
} EFI_FILE_PROTOCOL;

typedef struct {
    UINT64 Size;
    UINT64 FileSize;
    UINT64 PhysicalSize;
    /* timestamps, attribute and name follow; unused */
} EFI_FILE_INFO;

typedef struct EFI_SIMPLE_FILE_SYSTEM_PROTOCOL {
    UINT64 Revision;
    EFI_STATUS (EFIAPI *OpenVolume)(struct EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *, EFI_FILE_PROTOCOL **);
} EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;

/* ---- Block devices ---------------------------------------------------- */

#define EFI_BLOCK_IO_PROTOCOL_GUID \
    {0x964E5B21, 0x6459, 0x11D2, {0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B}}

typedef struct {
    UINT32 MediaId;
    BOOLEAN RemovableMedia, MediaPresent, LogicalPartition, ReadOnly, WriteCaching;
    UINT32 BlockSize;
    UINT32 IoAlign;
    UINT64 LastBlock;
} EFI_BLOCK_IO_MEDIA;

typedef struct EFI_BLOCK_IO_PROTOCOL {
    UINT64 Revision;
    EFI_BLOCK_IO_MEDIA *Media;
    void *Reset;
    EFI_STATUS (EFIAPI *ReadBlocks)(struct EFI_BLOCK_IO_PROTOCOL *, UINT32, UINT64, UINTN, void *);
} EFI_BLOCK_IO_PROTOCOL;

/* ---- Graphics --------------------------------------------------------- */

typedef enum {
    PixelRedGreenBlueReserved8BitPerColor,
    PixelBlueGreenRedReserved8BitPerColor,
    PixelBitMask,
    PixelBltOnly,
} EFI_GRAPHICS_PIXEL_FORMAT;

typedef struct {
    UINT32 Version;
    UINT32 HorizontalResolution;
    UINT32 VerticalResolution;
    EFI_GRAPHICS_PIXEL_FORMAT PixelFormat;
    UINT32 PixelInformation[4];
    UINT32 PixelsPerScanLine;
} EFI_GRAPHICS_OUTPUT_MODE_INFORMATION;

typedef struct {
    UINT32 MaxMode;
    UINT32 Mode;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info;
    UINTN SizeOfInfo;
    EFI_PHYSICAL_ADDRESS FrameBufferBase;
    UINTN FrameBufferSize;
} EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE;

typedef struct {
    void *QueryMode;
    void *SetMode;
    void *Blt;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *Mode;
} EFI_GRAPHICS_OUTPUT_PROTOCOL;

/* ---- PCI -------------------------------------------------------------- */

typedef enum { EfiPciIoWidthUint8, EfiPciIoWidthUint16, EfiPciIoWidthUint32 } EFI_PCI_IO_WIDTH;
typedef enum { EfiPciIoBusMasterRead, EfiPciIoBusMasterWrite, EfiPciIoBusMasterCommonBuffer } EFI_PCI_IO_OPERATION;
typedef enum {
    EfiPciIoAttributeOperationGet,
    EfiPciIoAttributeOperationSet,
    EfiPciIoAttributeOperationEnable,
    EfiPciIoAttributeOperationDisable,
    EfiPciIoAttributeOperationSupported,
} EFI_PCI_IO_ATTRIBUTE_OPERATION;

#define EFI_PCI_IO_ATTRIBUTE_MEMORY     0x0200ULL
#define EFI_PCI_IO_ATTRIBUTE_BUS_MASTER 0x0400ULL

struct EFI_PCI_IO_PROTOCOL;

typedef EFI_STATUS (EFIAPI *EFI_PCI_IO_MEM)(struct EFI_PCI_IO_PROTOCOL *, EFI_PCI_IO_WIDTH, UINT8, UINT64, UINTN, void *);
typedef EFI_STATUS (EFIAPI *EFI_PCI_IO_CFG)(struct EFI_PCI_IO_PROTOCOL *, EFI_PCI_IO_WIDTH, UINT32, UINTN, void *);

typedef struct { EFI_PCI_IO_MEM Read; EFI_PCI_IO_MEM Write; } EFI_PCI_IO_ACCESS;
typedef struct { EFI_PCI_IO_CFG Read; EFI_PCI_IO_CFG Write; } EFI_PCI_IO_CONFIG_ACCESS;

typedef struct EFI_PCI_IO_PROTOCOL {
    void *PollMem;
    void *PollIo;
    EFI_PCI_IO_ACCESS Mem;
    EFI_PCI_IO_ACCESS Io;
    EFI_PCI_IO_CONFIG_ACCESS Pci;
    void *CopyMem;
    EFI_STATUS (EFIAPI *Map)(struct EFI_PCI_IO_PROTOCOL *, EFI_PCI_IO_OPERATION, void *, UINTN *, EFI_PHYSICAL_ADDRESS *, void **);
    EFI_STATUS (EFIAPI *Unmap)(struct EFI_PCI_IO_PROTOCOL *, void *);
    EFI_STATUS (EFIAPI *AllocateBuffer)(struct EFI_PCI_IO_PROTOCOL *, EFI_ALLOCATE_TYPE, EFI_MEMORY_TYPE, UINTN, void **, UINT64);
    EFI_STATUS (EFIAPI *FreeBuffer)(struct EFI_PCI_IO_PROTOCOL *, UINTN, void *);
    void *Flush;
    void *GetLocation;
    EFI_STATUS (EFIAPI *Attributes)(struct EFI_PCI_IO_PROTOCOL *, EFI_PCI_IO_ATTRIBUTE_OPERATION, UINT64, UINT64 *);
} EFI_PCI_IO_PROTOCOL;

#endif
